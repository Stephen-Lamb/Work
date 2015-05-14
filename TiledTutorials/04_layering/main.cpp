#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <vector>
#include "base64.h"
#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_image.h"
#include "tinyxml2.h"
#include "zlib.h"

//////////////////////////////////////////////////////////////////////////////

const char* windowTitle = "04_layering";
const int windowWidth = 864;
const int windowHeight = 480;
const char* mapDir = "map/";
const char* mapName = "map.tmx";
const char* objectLayerName = "objectLayer";
const char* objectCollisionAreasName = "objectCollisionAreas";
const char* objectBoundingAreasName = "objectBoundingAreas";
const char* cubePath = "map/cubes.png";
const SDL_Rect cubeRects[] =
{
	{ 0, 64, 64, 32 }, {0, 0, 64, 32},
	{ 64, 64, 64, 32 }, { -32, 16, 64, 32 },
	{ 64, 96, 64, 32 }, { 32, 16, 64, 32 },
	{ 0, 96, 64, 32 }, { 0, 32, 64, 32 },
};
const SDL_Point cubeCollisionPolygon[] =
{
	{ 0, 48 },
	{ 32, 32 },
	{ 64, 48 },
	{ 32, 64 },
};
const int cubeInitX = 64;
const int cubeInitY = 32;
const int cubeVelocityInc = 1;

//////////////////////////////////////////////////////////////////////////////

// trim from right
inline std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v")
{
	s.erase(0, s.find_first_not_of(t));
	return s;
}

// trim from right
inline std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v")
{
	s.erase(s.find_last_not_of(t) + 1);
	return s;
}

// trim from left & right
inline std::string& trim(std::string& s, const char* t = " \t\n\r\f\v")
{
	return ltrim(rtrim(s, t), t);
}

template <typename T>
std::string numberToString(T num)
{
	std::stringstream ss;
	ss << num;
	return ss.str();
}

template <typename T>
T stringToNumber(const std::string &txt)
{
	std::stringstream ss(txt);
	T result;
	return ss >> result ? result : 0;
}

//////////////////////////////////////////////////////////////////////////////

// The following functions are based on the Separating Axis Theorem. For more
// details see:
// http://content.gpwiki.org/index.php/Polygon_Collision
// http://gamemath.com/2011/09/detecting-whether-two-convex-polygons-overlap/

// Dot product operator
int dot(const SDL_Point& a, const SDL_Point& b)
{
	return a.x * b.x + a.y * b.y;
}

// Gather up one-dimensional extents of the projection of the polygon
// onto this axis.
void gatherPolygonProjectionExtents(
	const std::vector<SDL_Point>& vertList,	// input polygon verts
	const SDL_Point& v,						// axis to project onto
	int& outMin, int& outMax				// 1D extents are output here
	)
{
	// Initialize extents to a single point, the first vertex
	outMin = outMax = dot(v, vertList[0]);

	// Now scan all the rest, growing extents to include them
	for (size_t i = 1; i < vertList.size(); ++i)
	{
		int d = dot(v, vertList[i]);
		if (d < outMin) outMin = d;
		else if (d > outMax) outMax = d;
	}
}

// Helper routine: test if two convex polygons overlap, using only the edges
// of the first polygon (polygon "a") to build the list of candidate
// separating axes.
bool findSeparatingAxis(const std::vector<SDL_Point>& aVertList,
	const std::vector<SDL_Point>& bVertList)
{
	// Iterate over all the edges
	size_t prev = aVertList.size() - 1;
	for (size_t cur = 0; cur < aVertList.size(); ++cur)
	{
		// Get edge vector.
		SDL_Point edge;
		edge.x = aVertList[cur].x - aVertList[prev].x;
		edge.y = aVertList[cur].y - aVertList[prev].y;

		// Rotate vector 90 degrees (doesn't matter which way) to get
		// candidate separating axis.
		SDL_Point v;
		v.x = edge.y;
		v.y = -edge.x;

		// Gather extents of both polygons projected onto this axis
		int aMin, aMax, bMin, bMax;
		gatherPolygonProjectionExtents(aVertList, v, aMin, aMax);
		gatherPolygonProjectionExtents(bVertList, v, bMin, bMax);

		// Is this a separating axis?
		if (aMax < bMin) return true;
		if (bMax < aMin) return true;

		// Next edge, please
		prev = cur;
	}

	// Failed to find a separating axis
	return false;
}

// Here is our high level entry point.  It tests whether two polygons
// intersect.  The polygons must be convex, and they must not be degenerate.
bool convexPolygonsIntersect(const std::vector<SDL_Point>& aVertList,
	const std::vector<SDL_Point>& bVertList)
{
	// First, use all of A's edges to get candidate separating axes
	if (findSeparatingAxis(aVertList, bVertList))
		return false;

	// Now swap roles, and use B's edges
	if (findSeparatingAxis(bVertList, aVertList))
		return false;

	// No separating axis found.  They must overlap
	return true;
}

//////////////////////////////////////////////////////////////////////////////

SDL_Rect aabbForPolygon(const std::vector<SDL_Point>& points)
{
	SDL_Rect re;
	SDL_EnclosePoints(&points[0], points.size(), NULL, &re);
	return re;
}

struct BoundingPoints
{
	SDL_Point left;
	SDL_Point top;
	SDL_Point right;
	SDL_Point bottom;
};

BoundingPoints boundingPointsForPolygon(
	const std::vector<SDL_Point>& vertList)
{
	BoundingPoints bp;
	bp.left = bp.top = bp.right = bp.bottom = vertList[0];

	for (size_t i = 1; i < vertList.size(); ++i)
	{
		if (vertList[i].x < bp.left.x)
			bp.left = vertList[i];

		if (vertList[i].y < bp.top.y)
			bp.top = vertList[i];

		if (vertList[i].x > bp.right.x)
			bp.right = vertList[i];

		if (vertList[i].y > bp.bottom.y)
			bp.bottom = vertList[i];
	}

	return bp;
}

bool isPointAboveComplexPolygon(const SDL_Point& p,
	const std::vector<SDL_Point>& vertList)
{
	size_t prev = vertList.size() - 1;
	for (size_t cur = 0; cur < vertList.size(); ++cur)
	{
		SDL_Point p1, p2;
		if (vertList[prev].x <= vertList[cur].x)
		{
			p1 = vertList[prev];
			p2 = vertList[cur];
		}
		else
		{
			p2 = vertList[prev];
			p1 = vertList[cur];
		}

		if (p1.x <= p.x && p.x <= p2.x)
		{
			if (p1.x == p2.x)
				return p.y < p1.y;

			// Use two point form for line equation to decide if point is
			// above line
			return p.y <
				p1.y + (float)(p2.y - p1.y) / (p2.x - p1.x) * (p.x - p1.x);
		}

		prev = cur;
	}

	return false;
}

// Note that this function provides undefined results if the point is on a
// boundary. If this is important to you then you should check for this
// beforehand. See the following for more details:
// http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
bool isPointInPolygon(const SDL_Point& p,
	const std::vector<SDL_Point>& polygon)
{
	bool inside = false;

	for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
	{
		if ( ((polygon[i].y > p.y) != (polygon[j].y > p.y)) &&
			(p.x < (float)(polygon[j].x - polygon[i].x) * (p.y - polygon[i].y)
				/ (polygon[j].y - polygon[i].y) + polygon[i].x) )
		{
			inside = !inside;
		}
	}

	return inside;
}

//////////////////////////////////////////////////////////////////////////////

class Texture
{
public:
	Texture();
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&&) = delete;
	Texture& operator=(Texture&&) = delete;

	bool load(const std::string& path, SDL_Renderer* renderer);
	void close();
	void render(SDL_Renderer* renderer, const SDL_Rect* srcRect = NULL,
		const SDL_Rect* destRect = NULL);

private:
	SDL_Texture* mTexture;
};

class Object
{
public:
	Object() = default;
	virtual ~Object() = default;

	Object(const Object&) = delete;
	Object& operator=(const Object&) = delete;
	Object(Object&&) = delete;
	Object& operator=(Object&&) = delete;

	virtual std::string name() const = 0;
	virtual std::vector<SDL_Point> collisionPolygon() const = 0;
	virtual void render(SDL_Renderer* renderer) = 0;
};

class Cube : public Object
{
public:
	Cube();
	~Cube();
	bool load(SDL_Renderer* renderer);
	void close();
	void handleEvent(const SDL_Event& event);
	void update();

	std::string name() const override { return "cube";  }
	std::vector<SDL_Point> collisionPolygon() const override;
	void render(SDL_Renderer* renderer) override;

private:
	int mX;
	int mY;
	int mVelocityX;
	int mVelocityY;
	Texture mTexture;
};

struct Tile
{
	int x;
	int y;
	uint32_t gid;
};

class TiledMap;

class StaticMapObject : public Object
{
public:
	StaticMapObject(TiledMap& tiledMap,
		const std::string& name,
		const std::vector<SDL_Point>& collisionPolygon,
		const std::vector<SDL_Point>& boundingPolygon) : mTiledMap(tiledMap),
		mName(name), mCollisionPolygon(collisionPolygon),
		mBoundingPolygon(boundingPolygon)
	{}

	std::string name() const override { return mName; }
	std::vector<SDL_Point> collisionPolygon() const override
		{ return mCollisionPolygon; }
	void render(SDL_Renderer* renderer) override;

	const std::vector<SDL_Point>& boundingPolygon() const
		{ return mBoundingPolygon; }
	void addTile(const Tile& tile) { mTiles.push_back(tile); }

private:
	TiledMap& mTiledMap;
	std::string mName;
	std::vector<SDL_Point> mCollisionPolygon;
	std::vector<SDL_Point> mBoundingPolygon;
	std::vector<Tile> mTiles;
};

struct Tileset
{
	uint32_t firstgid;
	struct Image
	{
		std::string source;
		int width;
		int height;
		Texture texture;
	} image;
};

struct Layer
{
	std::string name;
	std::vector<uint32_t> tileGids;
};

struct PolygonObject
{
	std::string name;
	std::vector<SDL_Point> points;
};

class TiledMap
{
public:
	TiledMap();
	~TiledMap();

	bool load(const std::string& dir, const std::string& fileName,
		SDL_Renderer* renderer);
	void close();
	void render(SDL_Renderer* renderer);

	const std::vector<PolygonObject>& collisionPolygons() const
		{ return mCollisionPolygons; }
	void addObject(Object* object) { mAddedObjects.push_back(object); }

private:
	void calcCoordsForTileIndex(size_t idx, const std::string& mapOrientation,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight, int& x, int& y);
	std::vector<SDL_Point> polygonForTile(int x, int y,
		const std::string& mapOrientation, int tileWidth, int tileHeight);
	bool isTileWithinBoundingArea(const std::vector<SDL_Point>& tilePolygon,
		const std::vector<SDL_Point>& boundingArea);
	void renderLayer(const Layer& layer, const std::string& mapOrientation,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight, SDL_Renderer* renderer);
	// Returns NULL if gid not found in any tileset
	Tileset* findTileset(const std::vector<Tileset*>& tilesets,
		uint32_t gid) const;
	void renderTile(int x, int y, Tileset& tileset, uint32_t gid,
		int tileWidth, int tileHeight, SDL_Renderer* renderer);

	bool readAllTilesets(tinyxml2::XMLConstHandle mapHandle,
		const std::string& dir, int tileWidth, int tileHeight,
		SDL_Renderer* renderer);
	bool readTileset(const tinyxml2::XMLElement* tilesetElement,
		const std::string& dir, int tileWidth, int tileHeight,
		SDL_Renderer* renderer);

	bool readAllLayers(tinyxml2::XMLConstHandle mapHandle,
		int mapWidthInTiles, int mapHeightInTiles);
	bool readLayer(const tinyxml2::XMLElement* layerElement,
		int mapWidthInTiles, int mapHeightInTiles);

	bool readPolygonAreas(tinyxml2::XMLConstHandle mapHandle);
	void initStaticMapObjects(const std::string& mapOrientation,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight);

	std::string mDir;
	std::string mMapOrientation;
	int mMapWidthInTiles;
	int mMapHeightInTiles;
	int mTileWidth;
	int mTileHeight;
	std::vector<Tileset*> mTilesets;
	std::vector<Layer> mLayers;
	std::vector<PolygonObject> mCollisionPolygons;
	std::vector<PolygonObject> mBoundingPolygons;
	std::vector<Object*> mAddedObjects;
	std::vector<StaticMapObject*> mStaticMapObjects;

	friend StaticMapObject;
};

//////////////////////////////////////////////////////////////////////////////

TiledMap gTiledMap;
Cube* gCube;

//////////////////////////////////////////////////////////////////////////////

Texture::Texture() : mTexture{ NULL }
{
}

Texture::~Texture()
{
	close();
}

bool Texture::load(const std::string& path, SDL_Renderer* renderer)
{
	close();

	SDL_Surface* surface = IMG_Load(path.c_str());
	if (!surface)
	{
		std::cerr << "load image failed\n";
		close();
		return false;
	}

	mTexture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_FreeSurface(surface);
	surface = NULL;

	if (!mTexture)
	{
		std::cerr << "create texture from surface failed\n";
		close();
		return false;
	}

	return true;
}

void Texture::close()
{
	if (mTexture)
	{
		SDL_DestroyTexture(mTexture);
		mTexture = NULL;
	}
}

void Texture::render(SDL_Renderer* renderer, const SDL_Rect* srcRect,
	const SDL_Rect* destRect)
{
	SDL_RenderCopy(renderer, mTexture, srcRect, destRect);
}

//////////////////////////////////////////////////////////////////////////////

Cube::Cube() : mX{ cubeInitX }, mY{ cubeInitY },
mVelocityX{ 0 }, mVelocityY{ 0 }
{
}

Cube::~Cube()
{
	close();
}

bool Cube::load(SDL_Renderer* renderer)
{
	close();

	if (!mTexture.load(cubePath, renderer))
	{
		std::cerr << "load texture failed\n";
		close();
		return false;
	}

	return true;
}

void Cube::close()
{
	mTexture.close();
}

void Cube::handleEvent(const SDL_Event& event)
{
	if (event.type == SDL_KEYDOWN && !event.key.repeat)
	{
		switch (event.key.keysym.sym)
		{
		case SDLK_LEFT:
			mVelocityX -= (cubeVelocityInc * 2);
			break;
		case SDLK_RIGHT:
			mVelocityX += (cubeVelocityInc * 2);
			break;
		case SDLK_UP:
			mVelocityY -= cubeVelocityInc;
			break;
		case SDLK_DOWN:
			mVelocityY += cubeVelocityInc;
			break;
		}
	}
	else if (event.type == SDL_KEYUP && !event.key.repeat)
	{
		switch (event.key.keysym.sym)
		{
		case SDLK_LEFT:
			mVelocityX += (cubeVelocityInc * 2);
			break;
		case SDLK_RIGHT:
			mVelocityX -= (cubeVelocityInc * 2);
			break;
		case SDLK_UP:
			mVelocityY += cubeVelocityInc;
			break;
		case SDLK_DOWN:
			mVelocityY -= cubeVelocityInc;
			break;
		}
	}
}

void Cube::update()
{
	mX += mVelocityX;
	mY += mVelocityY;

	// Check to see if there is a collision between the cube and other
	// collision polygons
	std::vector<SDL_Point> cubePoly = collisionPolygon();
	SDL_Rect cubeAabb = aabbForPolygon(cubePoly);

	bool collided = false;
	for (const PolygonObject& poly : gTiledMap.collisionPolygons())
	{
		SDL_Rect polyAabb = aabbForPolygon(poly.points);
		if (SDL_HasIntersection(&cubeAabb, &polyAabb)
			&& convexPolygonsIntersect(cubePoly, poly.points))
		{
			collided = true;
			break;
		}
	}

	if (collided)
	{
		// move back
		mX -= mVelocityX;
		mY -= mVelocityY;
	}
}

std::vector<SDL_Point> Cube::collisionPolygon() const
{
	std::vector<SDL_Point> cubePoly;
	const size_t numPoints =
		sizeof(cubeCollisionPolygon) / sizeof(cubeCollisionPolygon[0]);
	for (size_t i = 0; i < numPoints; ++i)
	{
		SDL_Point pt = cubeCollisionPolygon[i];
		pt.x += mX;
		pt.y += mY;
		cubePoly.push_back(pt);
	}

	return cubePoly;
}

void Cube::render(SDL_Renderer* renderer)
{
	const size_t numCubeRects = sizeof(cubeRects) / sizeof(cubeRects[0]);
	for (size_t i = 0; i < numCubeRects; i += 2)
	{
		SDL_Rect destRect = cubeRects[i + 1];
		destRect.x += mX;
		destRect.y += mY;
		mTexture.render(renderer, &cubeRects[i], &destRect);
	}
}

//////////////////////////////////////////////////////////////////////////////

void StaticMapObject::render(SDL_Renderer* renderer)
{
	for (const Tile& tile : mTiles)
	{
		Tileset* tileset = mTiledMap.findTileset(mTiledMap.mTilesets,
			tile.gid);
		if (tileset)
		{
			mTiledMap.renderTile(tile.x, tile.y, *tileset, tile.gid,
				mTiledMap.mTileWidth, mTiledMap.mTileHeight, renderer);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

TiledMap::TiledMap() : mMapWidthInTiles{ 0 }, mMapHeightInTiles{ 0 },
mTileWidth{ 0 }, mTileHeight{ 0 }
{
}

TiledMap::~TiledMap()
{
	close();
}

bool TiledMap::load(const std::string& dir, const std::string& fileName,
	SDL_Renderer* renderer)
{
	close();

	mDir = dir;
	std::string path = dir + fileName;
	tinyxml2::XMLDocument doc;
	doc.LoadFile(path.c_str());

	if (doc.Error())
	{
		std::cerr << "Load XML file failed, error = "
			<< doc.ErrorID() << "\n";
		close();
		return false;
	}

	tinyxml2::XMLConstHandle docHandle = &doc;
	const tinyxml2::XMLElement* mapElement =
		docHandle.FirstChildElement("map").ToElement();

	if (!mapElement)
	{
		std::cerr << "couldn't find map element\n";
		close();
		return false;
	}

	const char* orientation = mapElement->Attribute("orientation");
	if (!orientation)
	{
		std::cerr << "map orientation attribute not found\n";
		close();
		return false;
	}
	mMapOrientation = orientation;

	if (mapElement->QueryIntAttribute("width", &mMapWidthInTiles)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map width attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("height", &mMapHeightInTiles)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map height attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("tilewidth", &mTileWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map tile width attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("tileheight", &mTileHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map tile height attribute missing\n";
		close();
		return false;
	}

	tinyxml2::XMLConstHandle mapHandle = mapElement;

	if (!readAllTilesets(mapHandle, mDir, mTileWidth, mTileHeight, renderer))
	{
		std::cerr << "read all tilesets failed\n";
		close();
		return false;
	}

	if (!readAllLayers(mapHandle, mMapWidthInTiles, mMapHeightInTiles))
	{
		std::cerr << "read all layers failed\n";
		close();
		return false;
	}

	if (!readPolygonAreas(mapHandle))
	{
		std::cerr << "read polygon areas failed\n";
		close();
		return false;
	}

	initStaticMapObjects(mMapOrientation, mMapWidthInTiles, mMapHeightInTiles,
		mTileWidth, mTileHeight);

	return true;
}

void TiledMap::close()
{
	for (Object* object : mStaticMapObjects)
	{
		delete object;
	}
	mStaticMapObjects.clear();
	for (Object* object : mAddedObjects)
	{
		delete object;
	}
	mAddedObjects.clear();
	mBoundingPolygons.clear();
	mCollisionPolygons.clear();
	mLayers.clear();
	for (Tileset* tileset : mTilesets)
	{
		delete tileset;
	}
	mTilesets.clear();
	mTileHeight = 0;
	mTileWidth = 0;
	mMapHeightInTiles = 0;
	mMapWidthInTiles = 0;
	mMapOrientation = "";
	mDir = "";
}

void TiledMap::render(SDL_Renderer* renderer)
{
	for (const auto& layer : mLayers)
	{
		renderLayer(layer, mMapOrientation, mMapWidthInTiles,
			mMapHeightInTiles, mTileWidth, mTileHeight, renderer);
	}
}

void TiledMap::calcCoordsForTileIndex(size_t idx,
	const std::string& mapOrientation, int mapWidthInTiles,
	int mapHeightInTiles, int tileWidth, int tileHeight, int& x, int& y)
{
	if (mapOrientation == "orthogonal")
	{
		x = (idx % mapWidthInTiles) * tileWidth;
		y = (idx / mapWidthInTiles) * tileHeight;
	}
	else if (mapOrientation == "isometric")
	{
		x = (mapHeightInTiles - 1 + idx % mapWidthInTiles
			- idx / mapWidthInTiles) * (tileWidth / 2);
		y = (idx % mapWidthInTiles + idx / mapWidthInTiles)
			* (tileHeight / 2);
	}
	else // if (mapOrientation == "staggered")
	{
		x = (idx % mapWidthInTiles) * tileWidth;
		if ((idx / mapWidthInTiles) % 2 != 0)
			x += (tileWidth / 2);
		y = (idx / mapWidthInTiles) * (tileHeight / 2);
	}
}

std::vector<SDL_Point> TiledMap::polygonForTile(int x, int y,
	const std::string& mapOrientation, int tileWidth, int tileHeight)
{
	std::vector<SDL_Point> polygon;

	if (mapOrientation == "orthogonal")
	{
		polygon.push_back({ x, y });
		polygon.push_back({ x + tileWidth - 1, y });
		polygon.push_back({ x + tileWidth - 1, y + tileHeight - 1 });
		polygon.push_back({ x, y + tileHeight - 1 });
	}
	else // if (mapOrientation == "isometric" || mapOrientation == "staggered")
	{
		// Even tile heights assumed here otherwise the following is slightly
		// wrong

		const int halfTileWidth = tileWidth / 2;
		const int halfTileHeight = tileHeight / 2;

		polygon.push_back({ x, y + halfTileHeight - 1});
		polygon.push_back({ x + halfTileWidth - 2, y });
		polygon.push_back({ x + halfTileWidth + 1, y });
		polygon.push_back({ x + tileWidth - 1, y + halfTileHeight - 1 });
		polygon.push_back({ x + halfTileWidth + 1, y + tileHeight - 2 });
		polygon.push_back({ x + halfTileWidth - 2, y + tileHeight - 2 });
	}

	return polygon;
}

bool TiledMap::isTileWithinBoundingArea(
	const std::vector<SDL_Point>& tilePolygon,
	const std::vector<SDL_Point>& boundingArea)
{
	SDL_Rect aabbForTilePolygon = aabbForPolygon(tilePolygon);
	SDL_Rect aabbForBoundingArea = aabbForPolygon(boundingArea);

	if (!SDL_HasIntersection(&aabbForTilePolygon, &aabbForBoundingArea))
		return false;

	// A tile is considered enclosed by a bounding area if all of it's
	// vertexes are within the area

	for (const SDL_Point& pt : tilePolygon)
	{
		if (!isPointInPolygon(pt, boundingArea))
		{
			return false;
		}
	}

	return true;
}

// Is obj1 behind obj2?
bool isBehind(const Object* obj1, const Object* obj2)
{
	std::vector<SDL_Point> collisionPoly1 = obj1->collisionPolygon();
	std::vector<SDL_Point> collisionPoly2 = obj2->collisionPolygon();
	
	BoundingPoints boundingPoints1 = boundingPointsForPolygon(collisionPoly1);
	BoundingPoints boundingPoints2 = boundingPointsForPolygon(collisionPoly2);
	
	int x1min = boundingPoints1.left.x;
	int x1max = boundingPoints1.right.x;
	int y1min = boundingPoints1.top.y;
	int y1max = boundingPoints1.bottom.y;
	
	int x2min = boundingPoints2.left.x;
	int x2max = boundingPoints2.right.x;
	int y2min = boundingPoints2.top.y;
	int y2max = boundingPoints2.bottom.y;

	if (x1max < x2min || x2max < x1min)
		return false;
	
	if (y1max < y2min)
		return true;
	
	if (y2max < y1min)
		return false;
	
	if (x1max <= x2max)
	{
		return isPointAboveComplexPolygon(boundingPoints1.right,
			collisionPoly2);
	}
	
	return !isPointAboveComplexPolygon(boundingPoints2.right, collisionPoly1);
}

struct ObjectSortData
{
	bool visited;
	std::vector<Object*> objectsBehind;
	size_t depth;
};

void visit(Object* object, std::map<Object*, ObjectSortData>& objectsSortData,
	size_t& depth)
{
	ObjectSortData& sortData = objectsSortData[object];

	if (!sortData.visited)
	{
		sortData.visited = true;

		for (Object* objectBehind : sortData.objectsBehind)
		{
			visit(objectBehind, objectsSortData, depth);
		}

		sortData.depth = depth++;
	}
}

class ObjectSortCriterion
{
public:
	ObjectSortCriterion(std::map<Object*, ObjectSortData>& objectsSortData)
		: mObjectsSortData(objectsSortData) {}

	bool operator()(Object* obj1, Object* obj2)
	{
		return mObjectsSortData[obj1].depth < mObjectsSortData[obj2].depth;
	}

private:
	std::map<Object*, ObjectSortData>& mObjectsSortData;
};

void sortObjects(std::vector<Object*>& objects)
{
	// Sorts objects using a depth-first search, which is an alternative
	// algorithm for topological sorting. For more details see:
	//
	// http://en.wikipedia.org/wiki/Topological_sorting
	// https://mazebert.com/2013/04/18/isometric-depth-sorting/

	std::map<Object*, ObjectSortData> objectsSortData;

	for (size_t i = 0; i < objects.size(); ++i)
	{
		ObjectSortData& sortData = objectsSortData[objects[i]];
		sortData.visited = false;

		for (size_t j = 0; j < objects.size(); ++j)
		{
			if (i != j && isBehind(objects[j], objects[i]))
			{
				sortData.objectsBehind.push_back(objects[j]);
			}
		}

		sortData.depth = 0;
	}

	size_t depth = 0;

	for (Object* object : objects)
	{
		visit(object, objectsSortData, depth);
	}

	std::sort(objects.begin(), objects.end(),
		ObjectSortCriterion(objectsSortData));
}

void TiledMap::renderLayer(const Layer& layer,
	const std::string& mapOrientation, int mapWidthInTiles,
	int mapHeightInTiles, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	if (layer.name == objectLayerName)
	{
		// Special case: the object layer contains both objects that move
		// around the map and static objects such as trees, buildings etc, and
		// they all need to be rendered in order according to their depth

		std::vector<Object*> objects(mAddedObjects);
		for (StaticMapObject* staticMapObject : mStaticMapObjects)
		{
			objects.push_back(staticMapObject);
		}

		sortObjects(objects);

		// For debugging
		//std::cout << "Objects = { ";
		//for (Object* object : objects)
		//{
		//	std::cout << object->name() << " ";
		//}
		//std::cout << "}" << std::endl;

		for (Object* object : objects)
		{
			object->render(renderer);
		}
	}
	else
	{
		for (size_t i = 0; i < layer.tileGids.size(); ++i)
		{
			Tileset* tileset = findTileset(mTilesets, layer.tileGids[i]);
			if (tileset)
			{
				int x, y;
				calcCoordsForTileIndex(i, mapOrientation, mapWidthInTiles,
					mapHeightInTiles, tileWidth, tileHeight, x, y);
				renderTile(x, y, *tileset, layer.tileGids[i], tileWidth,
					tileHeight, renderer);
			}
		}
	}
}

Tileset* TiledMap::findTileset(const std::vector<Tileset*>& tilesets,
	uint32_t gid) const
{
	Tileset* tileset = NULL;

	for (size_t i = tilesets.size(); i > 0; --i)
	{
		if (gid >= tilesets[i - 1]->firstgid)
		{
			// found tileset
			tileset = tilesets[i - 1];
			break;
		}
	}

	return tileset;
}

void TiledMap::renderTile(int x, int y, Tileset& tileset, uint32_t gid,
	int tileWidth, int tileHeight, SDL_Renderer* renderer)
{
	const int imageWidthInTiles = tileset.image.width / tileWidth;
	const int numTiles = imageWidthInTiles *
		(tileset.image.height / tileHeight);
	const int imageWidth = imageWidthInTiles * tileWidth;
	int imageX = 0;
	int imageY = 0;

	// Look for tile in tileset
	for (int i = 0; i < numTiles; ++i)
	{
		if (gid == tileset.firstgid + i)
		{
			// found tile
			SDL_Rect srcRect;
			srcRect.x = imageX;
			srcRect.y = imageY;
			srcRect.w = tileWidth;
			srcRect.h = tileHeight;

			SDL_Rect dstRect;
			dstRect.x = x;
			dstRect.y = y;
			dstRect.w = tileWidth;
			dstRect.h = tileHeight;

			tileset.image.texture.render(renderer, &srcRect, &dstRect);

			break;
		}

		imageX += tileWidth;
		if (imageX >= imageWidth)
		{
			imageX = 0;
			imageY += tileHeight;
		}
	}
}

bool TiledMap::readAllTilesets(tinyxml2::XMLConstHandle mapHandle,
	const std::string& dir, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	const tinyxml2::XMLElement* tilesetElement =
		mapHandle.FirstChildElement("tileset").ToElement();

	while (tilesetElement)
	{
		if (!readTileset(tilesetElement, dir, tileWidth, tileHeight,
			renderer))
		{
			std::cerr << "read tileset failed\n";
			return false;
		}

		tilesetElement = tilesetElement->NextSiblingElement("tileset");
	}

	return true;
}

bool TiledMap::readTileset(const tinyxml2::XMLElement* tilesetElement,
	const std::string& dir, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	Tileset* tileset = new Tileset;

	if (tilesetElement->QueryUnsignedAttribute("firstgid", &tileset->firstgid)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset firstgid attribute missing\n";
		delete tileset;
		return false;
	}

	int tilesetTileWidth = 0;
	if (tilesetElement->QueryIntAttribute("tilewidth", &tilesetTileWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tilewidth attribute missing\n";
		delete tileset;
		return false;
	}

	int tilesetTileHeight = 0;
	if (tilesetElement->QueryIntAttribute("tileheight", &tilesetTileHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tileheight attribute missing\n";
		delete tileset;
		return false;
	}

	if (tilesetTileWidth != tileWidth || tilesetTileHeight != tileHeight)
	{
		std::cerr << "map/tileset tile size difference not supported\n";
		delete tileset;
		return false;
	}

	const tinyxml2::XMLElement* imageElement =
		tilesetElement->FirstChildElement("image");

	if (!imageElement)
	{
		std::cerr << "image element not found\n";
		delete tileset;
		return false;
	}

	const char* imageSource = imageElement->Attribute("source");
	if (!imageSource)
	{
		std::cerr << "image source attribute not found\n";
		delete tileset;
		return false;
	}
	tileset->image.source = imageSource;

	if (imageElement->QueryIntAttribute("width", &tileset->image.width)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "image width attribute missing\n";
		delete tileset;
		return false;
	}

	if (imageElement->QueryIntAttribute("height", &tileset->image.height)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "image height attribute missing\n";
		delete tileset;
		return false;
	}

	std::string imagePath = dir + tileset->image.source;
	if (!tileset->image.texture.load(imagePath, renderer))
	{
		std::cerr << "could not create image texture\n";
		delete tileset;
		return false;
	}

	mTilesets.push_back(tileset);

	return true;
}

bool TiledMap::readAllLayers(tinyxml2::XMLConstHandle mapHandle,
	int mapWidthInTiles, int mapHeightInTiles)
{
	const tinyxml2::XMLElement* layerElement =
		mapHandle.FirstChildElement("layer").ToElement();

	while (layerElement)
	{
		if (!readLayer(layerElement, mapWidthInTiles, mapHeightInTiles))
		{
			std::cerr << "read layer failed\n";
			return false;
		}

		layerElement = layerElement->NextSiblingElement("layer");
	}

	return true;
}

bool TiledMap::readLayer(const tinyxml2::XMLElement* layerElement,
	int mapWidthInTiles, int mapHeightInTiles)
{
	Layer layer;

	const char* nameCStr = layerElement->Attribute("name");
	if (nameCStr)
	{
		layer.name = nameCStr;
	}

	const tinyxml2::XMLElement* dataElement =
		layerElement->FirstChildElement("data");

	if (!dataElement)
	{
		std::cerr << "data element not found\n";
		return false;
	}

	if (!dataElement->Attribute("encoding")
		&& !dataElement->Attribute("compression"))
	{
		// No encoding or compression, tiles stored as individual XML elements
		const tinyxml2::XMLElement* tileElement =
			dataElement->FirstChildElement("tile");

		while (tileElement)
		{
			uint32_t gid = 0;
			if (tileElement->QueryUnsignedAttribute("gid", &gid)
				!= tinyxml2::XML_NO_ERROR)
			{
				std::cerr << "tile gid attribute missing\n";
				return false;
			}
			layer.tileGids.push_back(gid);

			tileElement = tileElement->NextSiblingElement("tile");
		}
	}
	else
	if (dataElement->Attribute("encoding", "base64")
		&& dataElement->Attribute("compression", "zlib"))
	{
		const char* encodedTextCStr = dataElement->GetText();
		if (!encodedTextCStr)
		{
			std::cerr << "text data not found\n";
			return false;
		}
		std::string encodedText(encodedTextCStr);
		trim(encodedText);
		std::string decodedText = base64_decode(encodedText);

		layer.tileGids.resize(mapWidthInTiles * mapHeightInTiles);
		unsigned long destLen = layer.tileGids.size() * sizeof(uint32_t);
		if (uncompress((unsigned char*)layer.tileGids.data(), &destLen,
			(const unsigned char*)decodedText.c_str(), decodedText.length())
			!= Z_OK || destLen != (mapWidthInTiles * mapHeightInTiles *
				sizeof(uint32_t)))
		{
			std::cerr << "zlib uncompress failed\n";
			return false;
		}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		// Swap from little-endian to big-endian byte ordering if needed
		for (auto& x : layer)
		{
			x = SDL_SwapLE32(x);
		}
#endif
	}
	else
	{
		std::cerr << "encoding type not supported\n";
		return false;
	}

	mLayers.push_back(layer);

	return true;
}

bool TiledMap::readPolygonAreas(tinyxml2::XMLConstHandle mapHandle)
{
	const tinyxml2::XMLElement* objGroupElement =
		mapHandle.FirstChildElement("objectgroup").ToElement();

	while (objGroupElement)
	{
		if (objGroupElement->Attribute("name", objectCollisionAreasName)
			|| objGroupElement->Attribute("name", objectBoundingAreasName))
		{
			const tinyxml2::XMLElement* objElement =
				objGroupElement->FirstChildElement("object");

			while (objElement)
			{
				PolygonObject polyObj;

				const char* nameCStr = objElement->Attribute("name");
				if (nameCStr)
				{
					polyObj.name = nameCStr;
				}

				int x = 0;
				if (objElement->QueryIntAttribute("x", &x)
					!= tinyxml2::XML_NO_ERROR)
				{
					std::cerr << "object x attribute missing\n";
					return false;
				}

				int y = 0;
				if (objElement->QueryIntAttribute("y", &y)
					!= tinyxml2::XML_NO_ERROR)
				{
					std::cerr << "object y attribute missing\n";
					return false;
				}

				const tinyxml2::XMLElement* polygonElement
					= objElement->FirstChildElement("polygon");

				if (polygonElement)
				{
					const char* pointsCStr
						= polygonElement->Attribute("points");

					if (!pointsCStr)
					{
						std::cerr << "polygon points attribute missing\n";
						return false;
					}

					std::string points = pointsCStr;
					std::regex reg("(\\-?\\d+)(?:\\.\\d+)?\\s*,\\s*(\\-?\\d+)(?:\\.\\d+)?");
					std::sregex_iterator pos(points.cbegin(), points.cend(),
						reg);
					std::sregex_iterator end;
					for (; pos != end; ++pos)
					{
						SDL_Point point;
						point.x = x + stringToNumber<int>(pos->str(1));
						point.y = y + stringToNumber<int>(pos->str(2));
						polyObj.points.push_back(point);
					}

					if (objGroupElement->Attribute("name",
						objectCollisionAreasName))
					{
						mCollisionPolygons.push_back(polyObj);
					}
					else
						mBoundingPolygons.push_back(polyObj);
				}

				objElement = objElement->NextSiblingElement("object");
			}
		}

		objGroupElement = objGroupElement->NextSiblingElement("objectgroup");
	}

	return true;
}

void TiledMap::initStaticMapObjects(const std::string& mapOrientation,
	int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
	int tileHeight)
{
	// Set-up associations between collision areas and bounding areas for
	// static objects on the map (used for rendering)

	const Layer* objectLayer = NULL;
	for (const Layer& layer : mLayers)
	{
		if (layer.name == objectLayerName)
		{
			objectLayer = &layer;
			break;
		}
	}

	if (!objectLayer)
		return;

	for (const PolygonObject& collisionPolygon : mCollisionPolygons)
	{
		for (const PolygonObject& boundingPolygon : mBoundingPolygons)
		{
			if (collisionPolygon.name == boundingPolygon.name)
			{
				mStaticMapObjects.push_back(new StaticMapObject(*this,
					collisionPolygon.name, collisionPolygon.points,
					boundingPolygon.points));

				break;
			}
		}
	}

	for (size_t tileIndex = 0; tileIndex < objectLayer->tileGids.size();
		++tileIndex)
	{
		int x, y;

		calcCoordsForTileIndex(tileIndex, mapOrientation,
			mapWidthInTiles, mapHeightInTiles, tileWidth, tileHeight,
			x, y);

		std::vector<SDL_Point> tilePolygon = polygonForTile(x, y,
			mapOrientation, tileWidth, tileHeight);

		for (StaticMapObject* staticMapObject : mStaticMapObjects)
		{
			if (isTileWithinBoundingArea(
				tilePolygon, staticMapObject->boundingPolygon()))
			{
				Tile tile;
				tile.x = x;
				tile.y = y;
				tile.gid = objectLayer->tileGids[tileIndex];
				staticMapObject->addTile(tile);

				break;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

bool loadAssets(SDL_Renderer* renderer)
{
	if (!gTiledMap.load(mapDir, mapName, renderer))
	{
		std::cerr << "Load tile map failed\n";
		return false;
	}

	gCube = new Cube();
	if (!gCube->load(renderer))
	{
		std::cerr << "Load cube failed\n";
		delete gCube;
		gCube = NULL;
		gTiledMap.close();
		return false;
	}

	gTiledMap.addObject(gCube);

	return true;
}

void destroyAssets()
{
	gTiledMap.close();
}

int main(int argc, char* argv[])
{
	int retVal = 0;

	try
	{
		if (SDL_Init(SDL_INIT_VIDEO) == 0)
		{
			int imgInitFlags = IMG_INIT_PNG;

			if ((IMG_Init(imgInitFlags) & imgInitFlags) == imgInitFlags)
			{
				SDL_Window* window = SDL_CreateWindow(windowTitle,
					SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
					windowWidth, windowHeight, 0);

				if (window != NULL)
				{
					SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
						SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

					if (renderer != NULL)
					{
						if (loadAssets(renderer))
						{
							// Game loop
							bool quit = false;

							while (!quit)
							{
								// Handle events
								SDL_Event event;

								while (SDL_PollEvent(&event))
								{
									switch (event.type)
									{
									case SDL_QUIT:
										quit = true;
										break;
									}

									gCube->handleEvent(event);
								}

								// Perform game logic here
								gCube->update();

								// Do rendering
								SDL_SetRenderDrawColor(renderer, 0, 0, 0,
									255);
								SDL_RenderClear(renderer);
								gTiledMap.render(renderer);
								// Note: the tile map is now responsible for
								// rendering our cube
								// gCube->render(renderer);
								SDL_RenderPresent(renderer);
							}

							destroyAssets();
						}
						else
						{
							std::cerr << "loadAssets failed\n";
							retVal = 1;
						}

						SDL_DestroyRenderer(renderer);
						renderer = NULL;
					}
					else
					{
						std::cerr << "SDL_CreateRenderer failed, error = "
							<< SDL_GetError() << "\n";
						retVal = 1;
					}

					SDL_DestroyWindow(window);
					window = NULL;
				}
				else
				{
					std::cerr << "SDL_CreateWindow failed, error = "
						<< SDL_GetError() << "\n";
					retVal = 1;
				}

				IMG_Quit();
			}
			else
			{
				std::cerr << "IMG_Init failed, error = "
					<< IMG_GetError() << "\n";
				retVal = 1;
			}

			SDL_Quit();
		}
		else
		{
			std::cerr << "SDL_Init failed, error = "
				<< SDL_GetError() << "\n";
			retVal = 1;
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Caught std::exception: " << e.what() << "\n";
		retVal = 1;
	}
	catch (...)
	{
		std::cerr << "Caught unknown exception\n";
		retVal = 1;
	}

	return retVal;
}
