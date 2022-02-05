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
	using namespace el1::math;

	struct tile_pos_t;

	using map_pos_t = TVector<s16_t, 2>;

	static map_pos_t MapPosFromString(const char* const str)
	{
		map_pos_t pos;
		if(sscanf(str, " ( %hd , %*d , %hd ) ", &pos[0], &pos[1]) != 2)
			throw "unable to parse map size";
		return pos;
	}

	struct tile_pos_t
	{
		map_pos_t tile;
		float tx, ty;

		void Normalize()
		{
			// FIXME: ... no comment ...
			while(tx > 0.5f)
			{
				tile[0]++;
				tx -= 1.0f;
			}

			while(tx < -0.5f)
			{
				tile[0]--;
				tx += 1.0f;
			}

			while(ty > 0.5f)
			{
				tile[1]++;
				ty -= 1.0f;
			}

			while(ty < -0.5f)
			{
				tile[1]--;
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

		tile_pos_t& operator/=(const float divider)
		{
			tx = (tile[0] + tx) / divider;
			ty = (tile[1] + ty) / divider;
			tile[0] = 0;
			tile[1] = 0;
			Normalize();
			return *this;
		}

		tile_pos_t operator/(const float divider) const
		{
			tile_pos_t r = *this;
			r /= divider;
			return r;
		}

		tile_pos_t(const map_pos_t pos = {0,0}, const float tx = 0.0f, const float ty = 0.0f) : tile({ pos[0], pos[1] }), tx(tx), ty(ty)
		{
		}
	};

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

	static tile_pos_t RimworldRotationToVector(const int rot)
	{
		switch(rot)
		{
			case 0: return TObstacleNode::DIRECTIONS[6];
			case 1: return TObstacleNode::DIRECTIONS[4];
			case 2: return TObstacleNode::DIRECTIONS[2];
			case 3: return TObstacleNode::DIRECTIONS[0];
			default: EL_THROW(TInvalidArgumentException, "rot");
		}
	}

	unsigned TObstacleNode::InvertDirection(const unsigned direction)
	{
		return (direction + N_DIRECTIONS/2) % N_DIRECTIONS;
	}

	/****************************************************************************/

	bool TObstacleMap::IsValidPosition(const map_pos_t pos) const
	{
		return pos[0] >= 0 && pos[1] >= 0 && pos[0] < size[0] && pos[1] < size[1];
	}

	tile_index_t TObstacleMap::PlaceObstacleAt(const map_pos_t pos, const EObstacleType type)
	{
		tile_index_t& index = this->array[pos[1] * this->size[0] + pos[0]];
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
			const tile_index_t index = this->array[pos[1] * this->size[0] + pos[0]];
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
			const tile_index_t index = this->array[pos[1] * this->size[0] + pos[0]];
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
				terminated_by_transition_or_processed_direction = true;
				return current_node;
			}

			if(neighbor->CrossNeighborsCount() > 2)
			{
				terminated_by_transition_or_processed_direction = false;
				neighbor->MarkDirectionProcessed(TObstacleNode::InvertDirection(direction));
				return neighbor;
			}

			current_node = neighbor;
			current_node->MarkDirectionProcessed(TObstacleNode::InvertDirection(direction));
			n_walk_distance++;
		}

		terminated_by_transition_or_processed_direction = false;
		current_node->MarkDirectionProcessed(direction);
		return current_node;
	}

	void TObstacleMap::ComputeObstacleGraph()
	{
		this->graph.Clear();

		for(usys_t i = 0; i < this->nodes.Count(); i++)
			this->nodes[i].UpdateNeighbors();

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
		array.Inflate(size[0] * size[1], INDEX_NONE);
	}

	/****************************************************************************/

	struct TMap
	{
		TObstacleMap obstacle_map;
		TList<light_source_t> lights;
		const map_pos_t size;
		map_pos_t image_pos;
		map_pos_t image_size;

		void ExportVTT(ostream& os, TFile& image);
		TMap(XMLElement* map_node);
	};

	TMap::TMap(XMLElement* map_node) : obstacle_map(MapPosFromString(map_node->FirstChildElement("mapInfo")->FirstChildElement("size")->GetText())), size(obstacle_map.Size())
	{
		cerr<<endl<<"map ID: "<<map_node->FirstChildElement("uniqueID")->UnsignedText()<<endl;
		cerr<<"size: ["<<this->size[0]<<"; "<<this->size[1]<<"]"<<endl;

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

		cerr<<"image area: pos = {"<<this->image_pos[0]<<"; "<<this->image_pos[1]<<"}, size = {"<<this->image_size[0]<<"; "<<this->image_size[1]<<"}"<<endl;

		unsigned n_walls = 0;
		unsigned n_windows = 0;
		unsigned n_doors = 0;
		unsigned n_terrain = 0;
		unsigned n_lights = 0;

		for(auto thing_node = map_node->FirstChildElement("things")->FirstChildElement("thing"); thing_node != nullptr; thing_node = thing_node->	NextSiblingElement())
		{
			if(thing_node->Attribute("Class") != nullptr)
			{
				const map_pos_t pos = thing_node->FirstChildElement("pos")->GetText() != nullptr ? MapPosFromString(thing_node->FirstChildElement("pos")->GetText()) : map_pos_t({0,0});

				if( strcmp(thing_node->Attribute("Class"), "Building") == 0 ||
					strcmp(thing_node->Attribute("Class"), "Building_Door") == 0 ||
					strcmp(thing_node->Attribute("Class"), "DubsBadHygiene.Building_StallDoor") == 0)
				{
					auto def_node = thing_node->FirstChildElement("def");
					if(def_node != nullptr)
					{

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
					n_terrain++;
					this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::TERRAIN_WALL);
				}
				else if(strcmp(thing_node->Attribute("Class"), "MURWallLight.WallLight") == 0)
				{
					n_lights++;
					auto rot_node = thing_node->FirstChildElement("rot");
					const int rot = (rot_node == nullptr) ? 0 : rot_node->Int64Text(0);
					this->lights.Append(light_source_t({pos + RimworldRotationToVector(rot).tile, 5}));
				}
			}
		}

		cerr<<"walls: "<<n_walls<<endl;
		cerr<<"doors: "<<n_doors<<endl;
		cerr<<"windows: "<<n_windows<<endl;
		cerr<<"terrain: "<<n_terrain<<endl;
		cerr<<"lights: "<<n_lights<<endl;

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
		os<<"\"map_size\":{ \"x\":"<<this->image_size[0]<<", \"y\":"<<this->image_size[1]<<" },"<<endl;
		os<<"\"pixels_per_grid\":64"<<endl;
		os<<"},"<<endl;
		os<<"\"line_of_sight\":["<<endl;

		bool first = true;

		for(usys_t i = 0; i < obstacles.Count(); i++)
		{
			if(obstacles[i].type == EObstacleType::TERRAIN_WALL || obstacles[i].type == EObstacleType::CONSTRUCTED_WALL)
			{
				const tile_pos_t from = obstacles[i].pos[0] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});
				const tile_pos_t to   = obstacles[i].pos[1] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});

				if(!first) os<<",";
				first = false;
				os<<"["<<endl;
				os<<"  { \"x\": "<<(from.tile[0] + from.tx)<<", \"y\": "<<(this->image_size[1] - (from.tile[1] + from.ty))<<" },"<<endl;
				os<<"  { \"x\": "<<(to.tile[0]   + to.tx  )<<", \"y\": "<<(this->image_size[1] - (to.tile[1]   + to.ty  ))<<" }"<<endl;
				os<<"]";;
				os<<endl;
			}
		}

		os<<"],"<<endl;
		os<<"\"portals\": ["<<endl;

		first = true;
		for(usys_t i = 0; i < obstacles.Count(); i++)
		{
			if(obstacles[i].type == EObstacleType::DOOR)
			{
				/*
					{
						" *position": {
							"x": 50,
							"y": 48.5
						},
						"bounds": [
							{
							"x": 50,
							"y": 48
							},
							{
							"x": 50,
							"y": 49
							}
						],
						"rotation": 4.712389,
						"closed": true,
						"freestanding": false
					},
				*/
				const tile_pos_t from = obstacles[i].pos[0] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});
				const tile_pos_t to   = obstacles[i].pos[1] - this->image_pos + tile_pos_t({{0,0},0.5f,0.5f});

				const tile_pos_t center = (from + to) / 2;

				if(!first) os<<",";
				first = false;
				os<<"{"<<endl;
				os<<"  \"position\": { \"x\": "<<(center.tile[0] + center.tx)<<", \"y\": "<<(this->image_size[1] - (center.tile[1] + center.ty))<<" },"<<endl;
				os<<"  \"bounds\": ["<<endl;
				os<<"    { \"x\": "<<(from.tile[0] + from.tx)<<", \"y\": "<<(this->image_size[1] - (from.tile[1] + from.ty))<<" },"<<endl;
				os<<"    { \"x\": "<<(to.tile[0]   + to.tx  )<<", \"y\": "<<(this->image_size[1] - (to.tile[1]   + to.ty  ))<<" }"<<endl;
				os<<"  ],"<<endl;
				os<<"  \"rotation\": 1,"<<endl;
				os<<"  \"closed\": true,"<<endl;
				os<<"  \"freestanding\": false"<<endl;
				os<<"}"<<endl;
			}
		}
		os<<"],"<<endl;
		os<<"\"environment\": { \"baked_lighting\": false, \"ambient_light\": \"00000000\" },"<<endl;

		/*
			{
				"position": {
					"x": 38.742462,
					"y": 44.529297
				},
				"range": 4.5,
				"intensity": 1,
				"color": "ffffad58",
				"shadows": true
			},
		*/

		os<<"\"lights\": ["<<endl;
		first = true;
		for(usys_t i = 0; i < lights.Count(); i++)
		{
			map_pos_t eff_pos = lights[i].pos - image_pos;
			eff_pos[1] = image_size[1] - eff_pos[1] - 1;
			if(!first) os<<",";
			first = false;
			os<<"{"<<endl;
			os<<"  \"position\": { \"x\": "<<eff_pos[0]<<".5, \"y\": "<<eff_pos[1]<<".5 },"<<endl;
			os<<"  \"range\": "<<lights[i].range<<","<<endl;
			os<<"  \"intensity\": 1,"<<endl;
			os<<"  \"color\": \"00000000\","<<endl;
			os<<"  \"shadows\": true"<<endl;
			os<<"}"<<endl;
		}
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
