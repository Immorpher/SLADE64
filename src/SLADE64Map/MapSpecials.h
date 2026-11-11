#pragma once

#include "SLADE64Map/MapObject/MapSector.h"

namespace slade
{
class MapVertex;
class MapThing;
class MapLine;
class SLADE64Map;
class ArchiveEntry;

class MapSpecials
{
public:
	void reset();

	void processMapSpecials(SLADE64Map* map) const;
	void processLineSpecial(MapLine* line) const;

	bool tagColour(int tag, ColRGBA* colour) const;
	bool tagFadeColour(int tag, ColRGBA* colour) const;
	bool tagColoursSet() const;
	bool tagFadeColoursSet() const;
	void updateTaggedSectors(const SLADE64Map* map) const;

	// ZDoom
	void processZDoomMapSpecials(SLADE64Map* map) const;
	void processZDoomLineSpecial(MapLine* line) const;
	void updateZDoomSector(MapSector* line);
	void processACSScripts(ArchiveEntry* entry);
	void setModified(const SLADE64Map* map, int tag) const;

private:
	struct SectorColour
	{
		int     tag;
		ColRGBA colour;
	};

	typedef std::map<MapVertex*, double> VertexHeightMap;

	vector<SectorColour> sector_colours_;
	vector<SectorColour> sector_fadecolours_;

	void processZDoomSlopes(SLADE64Map* map) const;
	void processEternitySlopes(const SLADE64Map* map) const;
	void processSRB2Slopes(const SLADE64Map* map) const;

	template<MapSector::SurfaceType>
	void applyPlaneAlign(MapLine* line, MapSector* target, MapSector* model_sector) const;
	template<MapSector::SurfaceType> void   applyLineSlopeThing(SLADE64Map* map, MapThing* thing) const;
	template<MapSector::SurfaceType> void   applySectorTiltThing(SLADE64Map* map, MapThing* thing) const;
	template<MapSector::SurfaceType> void   applyVavoomSlopeThing(SLADE64Map* map, MapThing* thing) const;
	template<MapSector::SurfaceType> double vertexHeight(MapVertex* vertex, MapSector* sector) const;
	template<MapSector::SurfaceType>
	void applyVertexHeightSlope(MapSector* target, vector<MapVertex*>& vertices, VertexHeightMap& heights) const;
};
} // namespace slade
