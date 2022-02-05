#include <tinyxml2.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdio.h>
#include "el1/gen/dbg/amalgam/el1.hpp"
#include "base64.h"

using namespace std;
using namespace tinyxml2;
using namespace el1::error;

namespace rim2vtt
{
	using namespace el1::io::types;
	using namespace el1::io::text::string;
	using namespace el1::io::collection::list;
	using namespace el1::io::stream;
	using namespace el1::io::file;

	struct tile_pos_t;

	struct map_pos_t
	{
		s16_t x, y;

		static map_pos_t FromString(const char* const str)
		{
			map_pos_t pos;
			if(sscanf(str, " ( %hd , %*d , %hd ) ", &pos.x, &pos.y) != 2)
				throw "unable to parse map size";
			return pos;
		}

		map_pos_t& operator+=(const map_pos_t& rhs)
		{
			x += rhs.x;
			y += rhs.y;
			return *this;
		}

		map_pos_t& operator-=(const map_pos_t& rhs)
		{
			x -= rhs.x;
			y -= rhs.y;
			return *this;
		}

		map_pos_t operator+(const map_pos_t& rhs) const
		{
			map_pos_t r = *this;
			r += rhs;
			return r;
		}

		map_pos_t operator-(const map_pos_t& rhs) const
		{
			map_pos_t r = *this;
			r -= rhs;
			return r;
		}

		operator tile_pos_t() const;
	};

	struct tile_pos_t
	{
		map_pos_t tile;
		float tx, ty;

		void Normalize()
		{
			if(tx > 0.5f)
			{
				tile.x++;
				tx -= 1.0f;
			}

			if(tx < -0.5f)
			{
				tile.x--;
				tx += 1.0f;
			}

			if(ty > 0.5f)
			{
				tile.y++;
				ty -= 1.0f;
			}

			if(ty < -0.5f)
			{
				tile.y--;
				ty += 1.0f;
			}
		}

		tile_pos_t& operator+=(const tile_pos_t& rhs)
		{
			tile += rhs.tile;
			tx += rhs.tx;
			ty += rhs.ty;
			Normalize();
			return *this;
		}

		tile_pos_t& operator-=(const tile_pos_t& rhs)
		{
			tile -= rhs.tile;
			tx -= rhs.tx;
			ty -= rhs.ty;
			Normalize();
			return *this;
		}

		tile_pos_t operator+(const tile_pos_t& rhs) const
		{
			tile_pos_t r = *this;
			r += rhs;
			return r;
		}

		tile_pos_t operator-(const tile_pos_t& rhs) const
		{
			tile_pos_t r = *this;
			r -= rhs;
			return r;
		}
	};

	map_pos_t::operator tile_pos_t() const
	{
		return tile_pos_t({ *this, 0.0f, 0.0f });
	}

	struct light_source_t
	{
		map_pos_t pos;
		float range;
	};

	enum class EObstacleType : u8_t
	{
		NONE,
		TERRAIN_WALL,
		CONSTRUCTED_WALL,
		WINDOW,
		DOOR
	};

	struct obstacle_t
	{
		tile_pos_t pos[2];
		EObstacleType type;
	};

	/****************************************************************************/

	using tile_index_t = u16_t;	// change this if you need more
	static const tile_index_t INDEX_NONE = (tile_index_t)-1;

	class TObstacleMap;
	class TObstacleNode;

	class TObstacleNode
	{
		protected:
			TObstacleMap* const map; // 8
			const map_pos_t pos; // 4
			const EObstacleType type; // 1
			u8_t mask_proccessed; // 1
			u8_t mask_neighbor; // 1
			u8_t n_all_neighbors : 4, // 1
				 n_cross_neighbors : 3;

			bool ComputeIsDoubleWall(const unsigned direction) const;

		public:
			static const unsigned N_DIRECTIONS = 8;
			static const tile_pos_t DIRECTIONS[N_DIRECTIONS];

			static unsigned InvertDirection(const unsigned original_direction);
			u8_t AllNeighborsCount() const { return this->n_all_neighbors; }
			u8_t CrossNeighborsCount() const { return this->n_cross_neighbors; }
			TObstacleMap* Map() { return this->map; }
			map_pos_t Position() const { return this->pos; }
			EObstacleType Type() const { return this->type; }
			bool HasUnprocessedDirections() const { return this->mask_proccessed != 255; }
			TObstacleNode* Neighbor(const unsigned direction);
			const TObstacleNode* Neighbor(const unsigned direction) const;
			bool HasNeighbor(const unsigned direction) const;
			bool WasDirectionProcessed(const unsigned direction) const;
			void MarkDirectionProcessed(const unsigned direction, const bool mark = true);
			void UpdateNeighbors();
			TObstacleNode(TObstacleMap* const map, const map_pos_t pos, const EObstacleType type) : map(map), pos(pos), type(type), mask_proccessed(0), mask_neighbor(0), n_all_neighbors(0), n_cross_neighbors(0) {}
	};

	class TObstacleMap
	{
		protected:
			TList<TObstacleNode> nodes;
			TList<tile_index_t> array;
			TList<obstacle_t> graph;
			const map_pos_t size;

			TObstacleNode* Walk(TObstacleNode& start_node, const unsigned direction, bool& terminated_by_transition_or_processed_direction);

		public:
			map_pos_t Size() const { return size; }
			bool IsValidPosition(const map_pos_t pos) const;
			tile_index_t PlaceObstacleAt(const map_pos_t pos, const EObstacleType type);
			TObstacleNode* operator[](const map_pos_t pos);
			const TObstacleNode* operator[](const map_pos_t pos) const;
			void ComputeObstacleGraph();
			const TList<const obstacle_t>& Graph() const { return this->graph; }

			TObstacleMap(const map_pos_t size);
	};

	/****************************************************************************/

	TObstacleNode* TObstacleNode::Neighbor(const unsigned direction)
	{
		return (*this->map)[this->pos + DIRECTIONS[direction].tile];
	}

	const TObstacleNode* TObstacleNode::Neighbor(const unsigned direction) const
	{
		return (*this->map)[this->pos + DIRECTIONS[direction].tile];
	}

	bool TObstacleNode::HasNeighbor(const unsigned direction) const
	{
		return ((this->mask_neighbor >> direction) & 1) != 0;
	}

	bool TObstacleNode::WasDirectionProcessed(const unsigned direction) const
	{
		return ((this->mask_proccessed >> direction) & 1) != 0;
	}

	void TObstacleNode::MarkDirectionProcessed(const unsigned direction, const bool mark)
	{
		if(mark)
			this->mask_proccessed |= (1 << direction);
		else
			this->mask_proccessed &= ~(1 << direction);
	}

	bool TObstacleNode::ComputeIsDoubleWall(const unsigned direction) const
	{
		// check half-circle around current position for obstacles of same typelib

		for(signed i = -2; i <= 2; i++)
		{
			signed check_direction = direction + i;
			if(check_direction < 0)
				check_direction += 8;
			else if(check_direction >= 8)
				check_direction -= 8;

			const TObstacleNode* const neighbor = this->Neighbor(check_direction);
			if(neighbor == nullptr || neighbor->Type() != this->Type())
				return false;
		}

		return true;
	}

	void TObstacleNode::UpdateNeighbors()
	{
		this->mask_proccessed = 0;
		this->mask_neighbor = 0;
		this->n_all_neighbors = 0;
		this->n_cross_neighbors = 0;
		for(unsigned i = 0; i < N_DIRECTIONS; i++)
		{
			const TObstacleNode* const neighbor = this->Neighbor(i);
			if(neighbor != nullptr && !this->ComputeIsDoubleWall(i) && !neighbor->ComputeIsDoubleWall(InvertDirection(i)))
			{
				this->n_all_neighbors++;
				this->mask_neighbor |= (1 << i);
			}
			else
				this->MarkDirectionProcessed(i);

			if((i % 2) == 0 && this->HasNeighbor(i))
				n_cross_neighbors++;
		}
	}

	// do not change order!
	const tile_pos_t TObstacleNode::DIRECTIONS[TObstacleNode::N_DIRECTIONS] = {
		{ {-1, 0}, -0.5f,  0.0f }, // WEST
		{ {-1,-1}, -0.5f, -0.5f }, // NORTH WEST
		{ { 0,-1},  0.0f, -0.5f }, // NORTH
		{ { 1,-1},  0.5f, -0.5f }, // NORTH EAST
		{ { 1, 0},  0.5f,  0.0f }, // EAST
		{ { 1, 1},  0.5f,  0.5f }, // SOUTH EAST
		{ { 0, 1},  0.0f,  0.5f }, // SOUTH
		{ {-1, 1}, -0.5f,  0.5f }, // SOUTH WEST
	};

	unsigned TObstacleNode::InvertDirection(const unsigned direction)
	{
		return (direction + N_DIRECTIONS/2) % N_DIRECTIONS;
	}

	/****************************************************************************/

	bool TObstacleMap::IsValidPosition(const map_pos_t pos) const
	{
		return pos.x >= 0 && pos.y >= 0 && pos.x < size.x && pos.y < size.y;
	}

	tile_index_t TObstacleMap::PlaceObstacleAt(const map_pos_t pos, const EObstacleType type)
	{
		tile_index_t& index = this->array[pos.y * this->size.x + pos.x];
		EL_ERROR(index != INDEX_NONE, TException, "cannot place obstacle at {%d; %d}: there is already an obstacle here");
		EL_ERROR(this->nodes.Count() >= (usys_t)((tile_index_t)-2), TException, TString::Format("too many obstacles on map (current: %d, limit: %d)", this->nodes.Count(), (tile_index_t)-2));

		this->nodes.Append(TObstacleNode(this, pos, type));
		index = this->nodes.Count() - 1;
		return index;
	}

	TObstacleNode* TObstacleMap::operator[](const map_pos_t pos)
	{
		if(this->IsValidPosition(pos))
		{
			const tile_index_t index = this->array[pos.y * this->size.x + pos.x];
			if(index != INDEX_NONE)
				return &this->nodes[index];
			else
				return nullptr;
		}
		else
			return nullptr;
	}

	const TObstacleNode* TObstacleMap::operator[](const map_pos_t pos) const
	{
		if(this->IsValidPosition(pos))
		{
			const tile_index_t index = this->array[pos.y * this->size.x + pos.x];
			if(index != INDEX_NONE)
				return &this->nodes[index];
			else
				return nullptr;
		}
		else
			return nullptr;
	}

	TObstacleNode* TObstacleMap::Walk(TObstacleNode& start_node, const unsigned direction, bool& terminated_by_transition_or_processed_direction)
	{
		unsigned n_walk_distance = 0;

		TObstacleNode* current_node = &start_node;
		while(current_node->HasNeighbor(direction))
		{
			current_node->MarkDirectionProcessed(direction);
			TObstacleNode* const neighbor = current_node->Neighbor(direction);

			if(neighbor->Type() != start_node.Type() || neighbor->WasDirectionProcessed(TObstacleNode::InvertDirection(direction)))
			{
				cerr<<"DEBUG: Walk(): n_walk_distance = "<<n_walk_distance;
				if(neighbor->Type() != start_node.Type())
					cerr<<"; terminating due to type-transition";
				if(neighbor->WasDirectionProcessed(TObstacleNode::InvertDirection(direction)))
					cerr<<"; terminating due to already processed direction";
				cerr<<endl;

				terminated_by_transition_or_processed_direction = true;
				return current_node;
			}

			if(neighbor->CrossNeighborsCount() > 2)
			{
				cerr<<"DEBUG: Walk(): n_walk_distance = "<<n_walk_distance<<"; terminating due to encountering node with more than two neighbors"<<endl;
				terminated_by_transition_or_processed_direction = false;
				neighbor->MarkDirectionProcessed(TObstacleNode::InvertDirection(direction));
				return neighbor;
			}

			current_node = neighbor;
			current_node->MarkDirectionProcessed(TObstacleNode::InvertDirection(direction));
			n_walk_distance++;
		}

		cerr<<"DEBUG: Walk(): n_walk_distance = "<<n_walk_distance<<"; terminating due to end of straight path reached"<<endl;
		terminated_by_transition_or_processed_direction = false;
		current_node->MarkDirectionProcessed(direction);
		return current_node;
	}

	void TObstacleMap::ComputeObstacleGraph()
	{
		this->graph.Clear();

		for(usys_t i = 0; i < this->nodes.Count(); i++)
			this->nodes[i].UpdateNeighbors();

		cerr<<"dumping neightbor map:"<<endl;
		for(s16_t y = 0; y < this->size.y; y++)
		{
			for(s16_t x = 0; x < this->size.x; x++)
			{
				TObstacleNode* node = (*this)[{x,y}];
				if(node == nullptr)
					cerr<<" ";
				else
					cerr<<((unsigned)node->CrossNeighborsCount());
			}
			cerr<<endl;
		}
		cerr<<"--------------------------------------------------"<<endl;


		// start at an obstructed tile with unprocessed directions
		// pick a unprocessed direction
		// walk until we hit a tile with n_neighbors > 2 or a different type or with the direction we are comming from already processed (possibly not moving at all)
		// walk the other direction from the starting tile until we hit a tile with n_neighbors > 2 or a different type or with the direction we are comming from already processed (possibly not moving at all)
		// mark both directions as processed in all tiles along the path
		// construct an obstacle_t
		// for each endpoint:
		//   - if the path was terminated by a type-transition or already processed direction
		//     - if the endpoint has more than 2 neighbors then create a junction in the center of the tile
		//         and create a second obstacle_t from the center towards the edge
		//     - else extend the obstacle_t to the edge of the tile
		//   - else terminate the obstacle_t at the center of the endpoint
		// find next obstructed tile with unprocessed directions
		// check if the current tile has more unprocessed directions
		// else find the next tile with unprocessed directions
		// NOTE: freestanding obstructed tiles ("columns" / "pillars") will not spawn any obstacle_t's

		for(usys_t idx_start_node = 0; idx_start_node < this->nodes.Count(); idx_start_node++)
		{
			TObstacleNode& start_node = this->nodes[idx_start_node];
			if(start_node.HasUnprocessedDirections())
			{
				for(unsigned direction = 0; direction < TObstacleNode::N_DIRECTIONS; direction += 2)
				{
					if(!start_node.WasDirectionProcessed(direction))
					{
						TObstacleNode* endpoints[2] = {};
						bool terminated_by_transition_or_processed_direction[2] = {};
						tile_pos_t endpoint_positions[2];
						unsigned endpoint_directions[2] = { direction, TObstacleNode::InvertDirection(direction) };

						endpoints[0] = Walk(start_node, endpoint_directions[0], terminated_by_transition_or_processed_direction[0]);
						endpoints[1] = start_node.CrossNeighborsCount() > 2 ? &start_node : Walk(start_node, endpoint_directions[1], terminated_by_transition_or_processed_direction[1]);

						for(unsigned idx_endpoint = 0; idx_endpoint < 2; idx_endpoint++)
						{
							TObstacleNode& endpoint = *endpoints[idx_endpoint];
							tile_pos_t& position = endpoint_positions[idx_endpoint];
							const unsigned endpoint_direction = endpoint_directions[idx_endpoint];
							position.tile = endpoint.Position();

							if(terminated_by_transition_or_processed_direction[idx_endpoint])
							{
								if(endpoint.CrossNeighborsCount() > 2)
								{
									// place obstacle at center
									position.tx = 0.0f;
									position.ty = 0.0f;

									// create second obstacle from center towards edge
									if(endpoints[0] != endpoints[1] || idx_endpoint == 0)
									{
										this->graph.Append(obstacle_t({
											{
												{ endpoint.Position(), 0.0f, 0.0f },
												{
													endpoint.Position(),
													TObstacleNode::DIRECTIONS[endpoint_direction].tx,
													TObstacleNode::DIRECTIONS[endpoint_direction].ty
												}
											},
											start_node.Type()
										}));
									}
								}
								else //if(endpoint.NeighborsCount() <= 2)
								{
									// place obstacle at edge
									position.tx = TObstacleNode::DIRECTIONS[endpoint_direction].tx;
									position.ty = TObstacleNode::DIRECTIONS[endpoint_direction].ty;
								}
							}
							else
							{
								// place at center
								position.tx = 0.0f;
								position.ty = 0.0f;
							}
						}

						this->graph.Append(obstacle_t({ { endpoint_positions[0], endpoint_positions[1] }, start_node.Type() }));
					}
				}
			}
		}
	}

	TObstacleMap::TObstacleMap(const map_pos_t size) : size(size)
	{
		array.Inflate(size.x * size.y, INDEX_NONE);
	}

	/****************************************************************************/

	struct TMap
	{
		TObstacleMap obstacle_map;
		const map_pos_t size;
		map_pos_t image_pos;
		map_pos_t image_size;

		void ExportVTT(ostream& os, TFile& image);
		TMap(XMLElement* map_node);
	};

	TMap::TMap(XMLElement* map_node) : obstacle_map(map_pos_t::FromString(map_node->FirstChildElement("mapInfo")->FirstChildElement("size")->GetText())), size(obstacle_map.Size())
	{
		cerr<<endl<<"map ID: "<<map_node->FirstChildElement("uniqueID")->UnsignedText()<<endl;
		cerr<<"size: ["<<this->size.x<<"; "<<this->size.y<<"]"<<endl;

		this->image_pos = {0,0};
		this->image_size = this->size;

		for(auto list_node = map_node->FirstChildElement("components")->FirstChildElement("li"); list_node != nullptr; list_node = list_node->	NextSiblingElement())
		{
			if(list_node->Attribute("Class") != nullptr && strcmp(list_node->Attribute("Class"), "ProgressRenderer.MapComponent_RenderManager") == 0)
			{
				this->image_pos = {
					(s16_t)list_node->FirstChildElement("rsTargetStartX")->Int64Text(-1),
					(s16_t)list_node->FirstChildElement("rsTargetStartZ")->Int64Text(-1)
				};

				const map_pos_t end = {
					(s16_t)list_node->FirstChildElement("rsTargetEndX")->Int64Text(-1),
					(s16_t)list_node->FirstChildElement("rsTargetEndZ")->Int64Text(-1)
				};

				this->image_size = end - this->image_pos;
				break;
			}
		}

		cerr<<"image area: pos = {"<<this->image_pos.x<<"; "<<this->image_pos.y<<"}, size = {"<<this->image_size.x<<"; "<<this->image_size.y<<"}"<<endl;

		unsigned n_walls = 0;
		unsigned n_windows = 0;
		unsigned n_doors = 0;
		unsigned n_terrain = 0;

		for(auto thing_node = map_node->FirstChildElement("things")->FirstChildElement("thing"); thing_node != nullptr; thing_node = thing_node->	NextSiblingElement())
		{
			if(thing_node->Attribute("Class") != nullptr)
			{
				if( strcmp(thing_node->Attribute("Class"), "Building") == 0 ||
					strcmp(thing_node->Attribute("Class"), "Building_Door") == 0 ||
					strcmp(thing_node->Attribute("Class"), "DubsBadHygiene.Building_StallDoor") == 0)
				{
					auto def_node = thing_node->FirstChildElement("def");
					if(def_node != nullptr)
					{
						const map_pos_t pos = map_pos_t::FromString(thing_node->FirstChildElement("pos")->GetText());

						if(strcmp(def_node->GetText(), "Wall") == 0)
						{
							n_walls++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::CONSTRUCTED_WALL);
						}
						else if(strcmp(def_node->GetText(), "Door") == 0 || strcmp(def_node->GetText(), "ToiletStallDoor") == 0)
						{
							n_doors++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::DOOR);
						}
						else if(strcmp(def_node->GetText(), "ED_Embrasure") == 0)
						{
							n_windows++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::WINDOW);
						}
					}
				}
				else if(strcmp(thing_node->Attribute("Class"), "Mineable") == 0)
				{
					const map_pos_t pos = map_pos_t::FromString(thing_node->FirstChildElement("pos")->GetText());
					n_terrain++;
					this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::TERRAIN_WALL);
				}
			}
		}

		cerr<<"walls: "<<n_walls<<endl;
		cerr<<"doors: "<<n_doors<<endl;
		cerr<<"windows: "<<n_windows<<endl;
		cerr<<"terrain: "<<n_terrain<<endl;

		this->obstacle_map.ComputeObstacleGraph();
		cerr<<"obstacles: "<<this->obstacle_map.Graph().Count()<<endl;
	}

	void TMap::ExportVTT(ostream& os, TFile& image)
	{
		const TList<const obstacle_t>& obstacles = this->obstacle_map.Graph();

		os<<"{"<<endl;;
		os<<"\"format\":0.2,"<<endl;
		os<<"\"resolution\":{"<<endl;
		os<<"\"map_origin\":{ \"x\":0, \"y\":0 },"<<endl;
		os<<"\"map_size\":{ \"x\":"<<this->image_size.x<<", \"y\":"<<this->image_size.y<<" },"<<endl;
		os<<"\"pixels_per_grid\":64"<<endl;
		os<<"},"<<endl;
		os<<"\"line_of_sight\":["<<endl;

		for(usys_t i = 0; i < obstacles.Count(); i++)
		{
			const tile_pos_t from = obstacles[i].pos[0] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});
			const tile_pos_t to   = obstacles[i].pos[1] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});

			os<<"["<<endl;
			os<<"  { \"x\": "<<(from.tile.x + from.tx)<<", \"y\": "<<(this->image_size.y - (from.tile.y + from.ty))<<" },"<<endl;
			os<<"  { \"x\": "<<(to.tile.x   + to.tx  )<<", \"y\": "<<(this->image_size.y - (to.tile.y   + to.ty  ))<<" }"<<endl;
			os<<"]";
			if(i < obstacles.Count() - 1)
				os<<",";
			os<<endl;
		}

		os<<"],"<<endl;
		os<<"\"portals\": [],"<<endl;
		os<<"\"environment\": { \"baked_lighting\": false, \"ambient_light\": \"00000000\" },"<<endl;
		os<<"\"lights\": ["<<endl;
		os<<"],"<<endl;

		TMapping mapping(&image);
		const int b64_size = Base64encode_len(mapping.Count());
		TList<byte_t> b64_data;
		b64_data.Inflate(b64_size, 0);
		EL_ERROR(Base64encode((char*)&b64_data[0], (const char*)&mapping[0], mapping.Count()) != b64_size, TLogicException);

		os<<"\"image\":\"";
		os.write((const char*)&b64_data[0], b64_size - 1);
		EL_ERROR(os.bad(), TException, "badbit set after write()");
		os<<"\""<<endl;
		os<<"}"<<endl;
	}
}

using namespace rim2vtt;


int main(int argc, char* argv[])
{
	try
	{
		if(argc < 3)
		{
			throw "missing argument";
		}

		XMLDocument doc;
		doc.LoadFile(argv[1]);
		TMap map(doc.RootElement()->FirstChildElement("game")->FirstChildElement("maps")->FirstChildElement("li"));
		TFile image(argv[2]);
		map.ExportVTT(cout, image);

		return 0;
	}
	catch(const char* msg)
	{
		cerr<<"ERROR: "<<msg<<endl;
	}
	catch(const IException& e)
	{
		cerr<<"ERROR: "<<e.Message().MakeCStr().get()<<endl;
	}

	return 1;
}
