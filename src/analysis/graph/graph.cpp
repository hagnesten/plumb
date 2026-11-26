#include "analysis/graph.h"
#include "base/sort/sort.h"

namespace analysis {

	int32_t function_graph::block_at ( uint64_t address ) const {

		int32_t low = 0;
		int32_t high = blocks.count ( ) - 1;

		while ( low <= high ) {

			int32_t middle = ( low + high ) / 2;
			const block& item = blocks [ static_cast < size_t > ( middle ) ];

			if ( address < item.start )
				high = middle - 1;
			else if ( address >= item.end )
				low = middle + 1;
			else
				return middle;

		}

		return -1;

	}

	static bool insert_sorted ( base::list < uint64_t >& items, uint64_t value ) {

		int32_t low = 0;
		int32_t high = items.count ( );

		while ( low < high ) {

			int32_t middle = ( low + high ) / 2;

			if ( items [ static_cast < size_t > ( middle ) ] < value )
				low = middle + 1;
			else
				high = middle;

		}

		if ( low < items.count ( ) && items [ static_cast < size_t > ( low ) ] == value )
			return false;

		items.push ( 0 );

		for ( int32_t i = items.count ( ) - 1; i > low; i-- )
			items [ static_cast < size_t > ( i ) ] = items [ static_cast < size_t > ( i - 1 ) ];

		items [ static_cast < size_t > ( low ) ] = value;

		return true;

	}

	struct pending_edge {

		uint64_t from;
		uint64_t to;
		edge_kind kind;
		int32_t value;

	};

	bool build_graph ( const database& db, uint64_t function, function_graph& out ) {

		out = function_graph ( );
		out.function = function;

		int32_t owner = db.function_starting ( function );

		if ( owner < 0 )
			return false;

		const function_info& info = db.functions [ static_cast < size_t > ( owner ) ];
		base::list < chunk_info > ranges;
		chunk_info main;

		main.start = info.start;
		main.end = info.end;
		main.owner = info.start;
		ranges.push ( main );

		for ( const chunk_info& item : db.chunks ) {

			if ( item.owner == function )
				ranges.push ( item );

		}

		auto inside = [ & ] ( uint64_t address ) {

			for ( const chunk_info& item : ranges ) {

				if ( address >= item.start && address < item.end )
					return true;

			}

			return false;

		};

		base::list < uint64_t > leaders;
		base::list < uint64_t > work;
		base::list < pending_edge > links;
		base::list < block > found;

		leaders.push ( function );
		work.push ( function );

		auto reach = [ & ] ( uint64_t from, uint64_t target, edge_kind kind, int32_t value ) {

			pending_edge link = { from, target, kind, value };

			links.push ( link );

			if ( insert_sorted ( leaders, target ) )
				work.push ( target );

		};

		while ( !work.empty ( ) && found.count ( ) < 20000 ) {

			uint64_t at = work.back ( );

			work.pop ( );

			block item;

			item.start = at;

			for ( ;; ) {

				x86::instruction ins;

				if ( !db.is_start ( at ) || !db.decode ( at, ins ) ) {

					item.leaves = true;

					break;

				}

				item.last = at;
				item.instructions++;

				uint64_t next = ins.next ( );
				bool stop = true;

				switch ( ins.control ) {

					case x86::flow::BRANCH:

						if ( ins.direct ( ) && inside ( ins.target ( ) ) )
							reach ( at, ins.target ( ), edge_kind::TAKEN, 0 );

						if ( inside ( next ) )
							reach ( at, next, edge_kind::NOT_TAKEN, 0 );

						break;

					case x86::flow::JUMP:

						if ( ins.direct ( ) ) {

							if ( inside ( ins.target ( ) ) )
								reach ( at, ins.target ( ), edge_kind::JUMP, 0 );
							else
								item.leaves = true;

						} else {

							int32_t table = db.table_for_jump ( at );

							if ( table >= 0 ) {

								const jump_table& cases = db.tables [ static_cast < size_t > ( table ) ];
								const loader::binary& file = *db.file;

								out.table = table;

								for ( uint32_t k = 0; k < cases.count; k++ ) {

									uint64_t offset = 0;

									if ( !file.offset_of ( cases.table + static_cast < uint64_t > ( k ) * cases.entry_size, offset ) )
										break;

									uint64_t raw = cases.entry_size == 8 ? file.read64 ( offset ) : file.read32 ( offset );
									uint64_t target = cases.type == table_kind::IMAGE_BASED ? file.base + static_cast < uint32_t > ( raw ) : ( cases.type == table_kind::TABLE_BASED ? cases.anchor + static_cast < uint64_t > ( static_cast < int64_t > ( static_cast < int32_t > ( raw ) ) ) : raw );

									if ( inside ( target ) )
										reach ( at, target, edge_kind::CASE, static_cast < int32_t > ( k ) );

								}

							} else
								item.leaves = true;

						}

						break;

					case x86::flow::RETURN:
						item.returns = true;
						break;

					case x86::flow::STOP:
						item.leaves = true;
						break;

					default:

						stop = false;

						if ( !inside ( next ) ) {

							item.leaves = true;
							stop = true;

						} else {

							int32_t low = 0;
							int32_t high = leaders.count ( );

							while ( low < high ) {

								int32_t middle = ( low + high ) / 2;

								if ( leaders [ static_cast < size_t > ( middle ) ] < next )
									low = middle + 1;
								else
									high = middle;

							}

							bool known = low < leaders.count ( ) && leaders [ static_cast < size_t > ( low ) ] == next;

							if ( db.is_label ( next ) || known ) {

								reach ( at, next, edge_kind::FALL, 0 );
								stop = true;

							}

						}

						break;

				}

				at = next;

				if ( stop )
					break;

			}

			item.end = at;
			found.push ( item );

		}

		base::sort ( found.data ( ), found.count ( ), [ ] ( const block& a, const block& b ) { return a.start < b.start; } );

		for ( int32_t i = 0; i < found.count ( ); i++ ) {

			block item = found [ static_cast < size_t > ( i ) ];

			if ( !out.blocks.empty ( ) && out.blocks.back ( ).start == item.start )
				continue;

			if ( i + 1 < found.count ( ) && found [ static_cast < size_t > ( i + 1 ) ].start < item.end && found [ static_cast < size_t > ( i + 1 ) ].start > item.start ) {

				uint64_t split = found [ static_cast < size_t > ( i + 1 ) ].start;
				uint64_t at = item.start;
				int32_t count = 0;
				uint64_t last = item.start;

				while ( at < split ) {

					x86::instruction ins;

					if ( !db.decode ( at, ins ) )
						break;

					last = at;
					count++;
					at = ins.next ( );

				}

				item.end = split;
				item.last = last;
				item.instructions = count;
				item.returns = false;
				item.leaves = false;

				pending_edge link = { last, split, edge_kind::FALL, 0 };

				links.push ( link );

			}

			out.blocks.push ( item );

		}

		base::list < edge > made;

		for ( const pending_edge& link : links ) {

			int32_t from = out.block_at ( link.from );
			int32_t to = out.block_at ( link.to );

			if ( from < 0 || to < 0 || out.blocks [ static_cast < size_t > ( to ) ].start != link.to || out.blocks [ static_cast < size_t > ( from ) ].last != link.from )
				continue;

			bool duplicate = false;

			for ( const edge& existing : made ) {

				if ( existing.from == from && existing.to == to && existing.kind == link.kind && existing.value == link.value )
					duplicate = true;

			}

			if ( duplicate )
				continue;

			edge item;

			item.from = from;
			item.to = to;
			item.kind = link.kind;
			item.value = link.value;
			made.push ( item );

		}

		for ( int32_t i = 0; i < out.blocks.count ( ); i++ ) {

			block& item = out.blocks [ static_cast < size_t > ( i ) ];

			item.first_out = out.edges.count ( );

			for ( const edge& link : made ) {

				if ( link.from == i )
					out.edges.push ( link );

			}

			item.out_count = out.edges.count ( ) - item.first_out;

		}

		return !out.blocks.empty ( );

	}

}