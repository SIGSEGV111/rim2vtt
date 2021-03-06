#include <tinyxml2.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdio.h>
#include "el1/gen/dbg/amalgam/el1.hpp"
#include "base64.h"
#include "zlib.h"

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
	using namespace el1::debug;

	using v2i_t = TVector<s16_t, 2>;
	using v2f_t = TVector<float, 2>;

	static v2i_t V2iFromRimworldPos(const char* const str)
	{
		v2i_t pos;
		EL_ERROR(sscanf(str, " ( %hd , %*d , %hd ) ", &pos[0], &pos[1]) != 2, TException, TString::Format("unable to parse %q as position", str));
		return pos;
	}

	struct light_source_t
	{
		v2i_t pos;
		float range;
	};

	enum class EObstacleType : u8_t
	{
		NONE,
		WALL,
		WINDOW,
		DOOR
	};

	struct obstacle_t
	{
		v2f_t pos[2];
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
			const v2i_t pos; // 4
			const EObstacleType type; // 1
			u8_t mask_proccessed; // 1
			u8_t mask_neighbor; // 1
			u8_t n_all_neighbors : 4, // 1
				 n_cross_neighbors : 3;

			bool ComputeIsDoubleWall(const unsigned direction) const;

		public:
			static const unsigned N_DIRECTIONS = 8;
			static const v2i_t MAP_DIRECTIONS[N_DIRECTIONS];
			static const v2f_t TILE_DIRECTIONS[N_DIRECTIONS];

			static unsigned InvertDirection(const unsigned original_direction);
			u8_t AllNeighborsCount() const { return this->n_all_neighbors; }
			u8_t CrossNeighborsCount() const { return this->n_cross_neighbors; }
			TObstacleMap* Map() { return this->map; }
			v2i_t Position() const { return this->pos; }
			EObstacleType Type() const { return this->type; }
			bool HasUnprocessedDirections() const { return this->mask_proccessed != 255; }
			TObstacleNode* Neighbor(const unsigned direction);
			const TObstacleNode* Neighbor(const unsigned direction) const;
			bool HasNeighbor(const unsigned direction) const;
			bool WasDirectionProcessed(const unsigned direction) const;
			void MarkDirectionProcessed(const unsigned direction, const bool mark = true);
			void UpdateNeighbors();
			TObstacleNode(TObstacleMap* const map, const v2i_t pos, const EObstacleType type) : map(map), pos(pos), type(type), mask_proccessed(0), mask_neighbor(0), n_all_neighbors(0), n_cross_neighbors(0) {}
	};

	class TObstacleMap
	{
		protected:
			TList<TObstacleNode> nodes;
			TList<tile_index_t> array;
			TList<obstacle_t> graph;
			const v2i_t size;

			TObstacleNode* Walk(TObstacleNode& start_node, const unsigned direction, bool& terminated_by_transition_or_processed_direction);

		public:
			v2i_t Size() const { return size; }
			bool IsValidPosition(const v2i_t pos) const;
			tile_index_t PlaceObstacleAt(const v2i_t pos, const EObstacleType type);
			TObstacleNode* operator[](const v2i_t pos);
			const TObstacleNode* operator[](const v2i_t pos) const;
			void ComputeObstacleGraph();
			const TList<const obstacle_t>& Graph() const { return this->graph; }

			TObstacleMap(const v2i_t size);
	};

	/****************************************************************************/

	TObstacleNode* TObstacleNode::Neighbor(const unsigned direction)
	{
		return (*this->map)[this->pos + MAP_DIRECTIONS[direction]];
	}

	const TObstacleNode* TObstacleNode::Neighbor(const unsigned direction) const
	{
		return (*this->map)[this->pos + MAP_DIRECTIONS[direction]];
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
	const v2i_t TObstacleNode::MAP_DIRECTIONS[TObstacleNode::N_DIRECTIONS] = {
		{-1, 0}, // WEST
		{-1,-1}, // NORTH WEST
		{ 0,-1}, // NORTH
		{ 1,-1}, // NORTH EAST
		{ 1, 0}, // EAST
		{ 1, 1}, // SOUTH EAST
		{ 0, 1}, // SOUTH
		{-1, 1}, // SOUTH WEST
	};

	// do not change order!
	const v2f_t TObstacleNode::TILE_DIRECTIONS[TObstacleNode::N_DIRECTIONS] = {
		{ -0.5f,  0.0f }, // WEST
		{ -0.5f, -0.5f }, // NORTH WEST
		{  0.0f, -0.5f }, // NORTH
		{  0.5f, -0.5f }, // NORTH EAST
		{  0.5f,  0.0f }, // EAST
		{  0.5f,  0.5f }, // SOUTH EAST
		{  0.0f,  0.5f }, // SOUTH
		{ -0.5f,  0.5f }, // SOUTH WEST
	};

	static v2i_t RimworldRotationToVector(const int rot)
	{
		switch(rot)
		{
			case 0: return TObstacleNode::MAP_DIRECTIONS[6];
			case 1: return TObstacleNode::MAP_DIRECTIONS[4];
			case 2: return TObstacleNode::MAP_DIRECTIONS[2];
			case 3: return TObstacleNode::MAP_DIRECTIONS[0];
			default: EL_THROW(TInvalidArgumentException, "rot");
		}
	}

	unsigned TObstacleNode::InvertDirection(const unsigned direction)
	{
		return (direction + N_DIRECTIONS/2) % N_DIRECTIONS;
	}

	/****************************************************************************/

	bool TObstacleMap::IsValidPosition(const v2i_t pos) const
	{
		return pos[0] >= 0 && pos[1] >= 0 && pos[0] < size[0] && pos[1] < size[1];
	}

	tile_index_t TObstacleMap::PlaceObstacleAt(const v2i_t pos, const EObstacleType type)
	{
		tile_index_t& index = this->array[pos[1] * this->size[0] + pos[0]];
		EL_ERROR(index != INDEX_NONE, TException, TString::Format("cannot place obstacle at {%d; %d}: there is already an obstacle here (current-type: %d, wanted-type: %d)", pos[0], pos[1], (u8_t)this->nodes[index].Type(), (u8_t)type));
		EL_ERROR(this->nodes.Count() >= (usys_t)((tile_index_t)-2), TException, TString::Format("too many obstacles on map (current: %d, limit: %d)", this->nodes.Count(), (tile_index_t)-2));

		this->nodes.Append(TObstacleNode(this, pos, type));
		index = this->nodes.Count() - 1;
		return index;
	}

	TObstacleNode* TObstacleMap::operator[](const v2i_t pos)
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

	const TObstacleNode* TObstacleMap::operator[](const v2i_t pos) const
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
						v2f_t endpoint_positions[2];
						unsigned endpoint_directions[2] = { direction, TObstacleNode::InvertDirection(direction) };

						endpoints[0] = Walk(start_node, endpoint_directions[0], terminated_by_transition_or_processed_direction[0]);
						endpoints[1] = start_node.CrossNeighborsCount() > 2 ? &start_node : Walk(start_node, endpoint_directions[1], terminated_by_transition_or_processed_direction[1]);

						for(unsigned idx_endpoint = 0; idx_endpoint < 2; idx_endpoint++)
						{
							TObstacleNode& endpoint = *endpoints[idx_endpoint];
							v2f_t& position = endpoint_positions[idx_endpoint];
							const unsigned endpoint_direction = endpoint_directions[idx_endpoint];

							if(terminated_by_transition_or_processed_direction[idx_endpoint])
							{
								if(endpoint.CrossNeighborsCount() > 2)
								{
									// place obstacle at center
									position = (v2f_t)endpoint.Position();

									// create second obstacle from center towards edge
									if(endpoints[0] != endpoints[1] || idx_endpoint == 0)
									{
										this->graph.Append(obstacle_t({
											{
												(v2f_t)endpoint.Position(),
												(v2f_t)endpoint.Position() + TObstacleNode::TILE_DIRECTIONS[endpoint_direction]
											},
											start_node.Type()
										}));
									}
								}
								else //if(endpoint.NeighborsCount() <= 2)
								{
									// place obstacle at edge
									position = (v2f_t)endpoint.Position() + TObstacleNode::TILE_DIRECTIONS[endpoint_direction];
								}
							}
							else
							{
								// place at center
								position = (v2f_t)endpoint.Position();
							}
						}

						this->graph.Append(obstacle_t({ { endpoint_positions[0], endpoint_positions[1] }, start_node.Type() }));
					}
				}
			}
		}
	}

	TObstacleMap::TObstacleMap(const v2i_t size) : size(size)
	{
		array.Inflate(size[0] * size[1], INDEX_NONE);
	}

	/****************************************************************************/

	struct TMap
	{
		TObstacleMap obstacle_map;
		TList<light_source_t> lights;
		const v2i_t size;
		v2i_t image_pos;
		v2i_t image_size;

		bool IsWithinImageArea(const v2i_t pos) const
		{
			return pos.AllBiggerEqual(image_pos) && pos.AllLess(image_pos + image_size);
		}

		bool IsWithinImageArea(const v2f_t pos) const
		{
			return pos.AllBiggerEqual((v2f_t)image_pos - v2f_t({1.0f,1.0f})) && pos.AllLess((v2f_t)image_pos + (v2f_t)image_size + v2f_t({1.0f,1.0f}));
		}

		void ExportVTT(ostream& os, TFile* const image);
		TMap(XMLElement* map_node);
	};

	static bool IsBase64Char(const char chr)
	{
		return (chr >= 'A' && chr <= 'Z') || (chr >= 'a' && chr <= 'z') || (chr >= '0' && chr <= '9') || chr == '+' || chr == '/' || chr == '=';
	}

	TMap::TMap(XMLElement* map_node) : obstacle_map(V2iFromRimworldPos(map_node->FirstChildElement("mapInfo")->FirstChildElement("size")->GetText())), size(obstacle_map.Size())
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

				const v2i_t end = {
					(s16_t)list_node->FirstChildElement("rsTargetEndX")->Int64Text(-1),
					(s16_t)list_node->FirstChildElement("rsTargetEndZ")->Int64Text(-1)
				};

				this->image_size = end - this->image_pos;
				break;
			}
		}

		cerr<<"image area: pos = {"<<this->image_pos[0]<<"; "<<this->image_pos[1]<<"}, size = {"<<this->image_size[0]<<"; "<<this->image_size[1]<<"}"<<endl;

		EL_ERROR(this->image_size[0] > this->size[0] || this->image_size[1] > this->size[1], TException, "image size is bigger than map size");

		unsigned n_walls = 0;
		unsigned n_windows = 0;
		unsigned n_doors = 0;
		unsigned n_terrain = 0;
		unsigned n_lights = 0;

		{
			TList<u16_t> terrain_grid_data;
			terrain_grid_data.Inflate(this->size[0] * this->size[1], 0);

			auto terrain_node = map_node->FirstChildElement("compressedThingMapDeflate");
			EL_ERROR(terrain_node == nullptr, TException, "no <compressedThingMapDeflate> node found");

			TList<char> base64_data;
			const char* base64_text_src = terrain_node->GetText();
			for(const char* p = base64_text_src; *p != 0; p++)
				if(IsBase64Char(*p))
					base64_data.Append(*p);

			TList<byte_t> raw_data;
			raw_data.Append(0x78);
			raw_data.Append(0x9c);
			raw_data.Inflate(Base64decode_len(&base64_data[0]), 0);
			int ret = Base64decode((char*)&raw_data[2], &base64_data[0]);
			EL_ERROR(ret < 0 || ret > (int)raw_data.Count(), TLogicException);
			raw_data.Cut(0, raw_data.Count() - ret);
			base64_data.Clear();

			unsigned long uncompressed_size = terrain_grid_data.Count() * 2;
			uncompress((byte_t*)&terrain_grid_data[0], &uncompressed_size, (byte_t*)&raw_data[0], raw_data.Count());
			EL_ERROR(uncompressed_size > terrain_grid_data.Count() * 2, TLogicException);

			for(s16_t y = 0; y < size[1]; y++)
			{
				for(s16_t x = 0; x < size[0]; x++)
				{
					if(terrain_grid_data[y * size[0] + x] != 0)
					{
						n_terrain++;
						this->obstacle_map.PlaceObstacleAt({x,y}, EObstacleType::WALL);
					}
				}
			}
		}

		for(auto thing_node = map_node->FirstChildElement("things")->FirstChildElement("thing"); thing_node != nullptr; thing_node = thing_node->	NextSiblingElement())
		{
			if(thing_node->Attribute("Class") != nullptr)
			{
				const v2i_t pos = thing_node->FirstChildElement("pos")->GetText() != nullptr ? V2iFromRimworldPos(thing_node->FirstChildElement("pos")->GetText()) : v2i_t({0,0});

				if( strcmp(thing_node->Attribute("Class"), "Building") == 0 ||
					strcmp(thing_node->Attribute("Class"), "Building_Door") == 0 ||
					strcmp(thing_node->Attribute("Class"), "DubsBadHygiene.Building_StallDoor") == 0)
				{
					auto def_node = thing_node->FirstChildElement("def");
					if(def_node != nullptr)
					{
						if(strcmp(def_node->GetText(), "Wall") == 0 || strcmp(def_node->GetText(), "RadiationShielding") == 0)
						{
							n_walls++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::WALL);
						}
						else if(strcmp(def_node->GetText(), "Door") == 0 || strcmp(def_node->GetText(), "ToiletStallDoor") == 0 || strcmp(def_node->GetText(), "DU_Blastdoor") == 0 || strcmp(def_node->GetText(), "Autodoor") == 0)
						{
							n_doors++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::DOOR);
						}
						else if(strcmp(def_node->GetText(), "ED_Embrasure") == 0)
						{
							n_windows++;
							this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::WINDOW);
						}
						else if(strcmp(def_node->GetText(), "TorchLamp") == 0)
						{
							n_lights++;
							this->lights.Append(light_source_t({pos, 4}));
						}
					}
				}
				else if(strcmp(thing_node->Attribute("Class"), "Mineable") == 0)
				{
					n_terrain++;
					this->obstacle_map.PlaceObstacleAt(pos, EObstacleType::WALL);
				}
				else if(strcmp(thing_node->Attribute("Class"), "MURWallLight.WallLight") == 0)
				{
					n_lights++;
					auto rot_node = thing_node->FirstChildElement("rot");
					const int rot = (rot_node == nullptr) ? 0 : rot_node->Int64Text(0);
					this->lights.Append(light_source_t({pos + RimworldRotationToVector(rot), 6}));
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

	void TMap::ExportVTT(ostream& os, TFile* const image)
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
			if(obstacles[i].type == EObstacleType::WALL)
			{
				if(IsWithinImageArea(obstacles[i].pos[0]) || IsWithinImageArea(obstacles[i].pos[1]))
				{
					const v2f_t from = obstacles[i].pos[0] - (v2f_t)this->image_pos + v2f_t({0.5f,0.5f});
					const v2f_t to   = obstacles[i].pos[1] - (v2f_t)this->image_pos + v2f_t({0.5f,0.5f});

					if(!first) os<<",";
					first = false;
					os<<"["<<endl;
					os<<"  { \"x\": "<<from[0]<<", \"y\": "<<(this->image_size[1] - from[1])<<" },"<<endl;
					os<<"  { \"x\": "<<to[0]  <<", \"y\": "<<(this->image_size[1] - to[1]  )<<" }"<<endl;
					os<<"]";;
					os<<endl;
				}
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

				if(IsWithinImageArea(obstacles[i].pos[0]) || IsWithinImageArea(obstacles[i].pos[1]))
				{
					const v2f_t from = obstacles[i].pos[0] - (v2f_t)this->image_pos + v2f_t({0.5f,0.5f});
					const v2f_t to   = obstacles[i].pos[1] - (v2f_t)this->image_pos + v2f_t({0.5f,0.5f});
					const v2f_t center = (from + to) / 2.0f;

					if(!first) os<<",";
					first = false;
					os<<"{"<<endl;
					os<<"  \"position\": { \"x\": "<<center[0]<<", \"y\": "<<(this->image_size[1] - center[1])<<" },"<<endl;
					os<<"  \"bounds\": ["<<endl;
					os<<"    { \"x\": "<<from[0]<<", \"y\": "<<(this->image_size[1] - from[1])<<" },"<<endl;
					os<<"    { \"x\": "<<to[0]  <<", \"y\": "<<(this->image_size[1] - to[1]  )<<" }"<<endl;
					os<<"  ],"<<endl;
					os<<"  \"rotation\": 1,"<<endl;
					os<<"  \"closed\": true,"<<endl;
					os<<"  \"freestanding\": false"<<endl;
					os<<"}"<<endl;
				}
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
			if(IsWithinImageArea(lights[i].pos))
			{
				v2i_t eff_pos = lights[i].pos - image_pos;
				eff_pos[1] = image_size[1] - eff_pos[1] - 1;
				if(!first) os<<",";
				first = false;
				os<<"{"<<endl;
				os<<"  \"position\": { \"x\": "<<eff_pos[0]<<".5, \"y\": "<<eff_pos[1]<<".5 },"<<endl;
				os<<"  \"range\": "<<(lights[i].range/4.0f)<<","<<endl;
				os<<"  \"intensity\": 1,"<<endl;
				os<<"  \"color\": \"00000000\","<<endl;
				os<<"  \"shadows\": true"<<endl;
				os<<"}"<<endl;
			}
		}
		os<<"],"<<endl;

		if(image != nullptr)
		{
			TMapping mapping(image);
			const int b64_size = Base64encode_len(mapping.Count());
			TList<byte_t> b64_data;
			b64_data.Inflate(b64_size, 0);
			EL_ERROR(Base64encode((char*)&b64_data[0], (const char*)&mapping[0], mapping.Count()) != b64_size, TLogicException);

			os<<"\"image\":\"";
			os.write((const char*)&b64_data[0], b64_size - 1);
			EL_ERROR(os.bad(), TException, "badbit set after write()");
			os<<"\""<<endl;
		}
		else
			os<<"\"image\":null"<<endl;
		os<<"}"<<endl;
	}
}

using namespace rim2vtt;


int main(int argc, char* argv[])
{
	try
	{
		XMLDocument doc;
		unique_ptr<TFile> image = nullptr;

		if(argc == 3)
		{
			EL_ERROR(doc.LoadFile(argv[1]) != XML_SUCCESS, TException, "unable to load savegame XML");
			image = unique_ptr<TFile>(new TFile(argv[2]));
		}
		else if(argc == 2)
		{
			EL_ERROR(doc.LoadFile(argv[1]) != XML_SUCCESS, TException, "unable to load savegame XML");
		}
		else if(argc == 1)
		{
			EL_ERROR(doc.LoadFile(stdin) != XML_SUCCESS, TException, "unable to load savegame XML");
		}
		else
			EL_THROW(TException, TString::Format("got unexpected number of arguments (got: %d, expected: 1 to 3)", argc));

		TMap map(doc.RootElement()->FirstChildElement("game")->FirstChildElement("maps")->FirstChildElement("li"));
		map.ExportVTT(cout, image.get());

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
