#include <iostream>
#include <vector>
#include "base64.h"
#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_image.h"
#include "tinyxml2.h"
#include "zlib.h"

//////////////////////////////////////////////////////////////////////////////

const char* windowTitle = "Dimetric 1";
const int windowWidth = 640;
const int windowHeight = 480;
const char* mapDir = "map/";
const char* mapName = "map.tmx";

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

//////////////////////////////////////////////////////////////////////////////

class TiledMap
{
public:
	TiledMap();
	~TiledMap();

	bool load(const std::string& dir, const std::string& fileName,
		SDL_Renderer* renderer);
	void close();
	void render(SDL_Renderer* renderer);

private:
	struct Tileset
	{
		uint32_t firstgid;
		struct Image
		{
			std::string source;
			int width;
			int height;
			SDL_Texture* texture;
		} image;
	};

	void renderLayer(const std::vector<uint32_t>& layer,
		const std::string& mapOrientation, int mapWidthInTiles,
		int mapHeightInTiles, int tileWidth, int tileHeight,
		SDL_Renderer* renderer);
	void renderOrthogonalLayer(const std::vector<uint32_t>& layer,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight, SDL_Renderer* renderer);
	void renderIsometricLayer(const std::vector<uint32_t>& layer,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight, SDL_Renderer* renderer);
	void renderStaggeredLayer(const std::vector<uint32_t>& layer,
		int mapWidthInTiles, int mapHeightInTiles, int tileWidth,
		int tileHeight, SDL_Renderer* renderer);
	// Returns NULL if gid not found in any tileset
	const Tileset* findTileset(const std::vector<Tileset>& tilesets,
		uint32_t gid) const;
	void renderTile(int x, int y, const Tileset& tileset, uint32_t gid,
		int tileWidth, int tileHeight, SDL_Renderer* renderer);

	bool readAllTilesets(tinyxml2::XMLConstHandle mapHandle,
		const std::string& dir, int tileWidth, int tileHeight,
		SDL_Renderer* renderer);
	bool readTileset(const tinyxml2::XMLElement* tilesetElement,
		const std::string& dir, int tileWidth, int tileHeight,
		SDL_Renderer* renderer);
	SDL_Texture* createTexture(const std::string& dir,
		const std::string& imageSource, SDL_Renderer* renderer);

	bool readAllLayers(tinyxml2::XMLConstHandle mapHandle,
		int mapWidthInTiles, int mapHeightInTiles);
	bool readLayer(const tinyxml2::XMLElement* layerElement,
		int mapWidthInTiles, int mapHeightInTiles);

	std::string mDir;
	std::string mMapOrientation;
	int mMapWidthInTiles;
	int mMapHeightInTiles;
	int mTileWidth;
	int mTileHeight;
	std::vector<Tileset> mTilesets;
	std::vector<std::vector<uint32_t> > mLayers;
};

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

	return true;
}

void TiledMap::close()
{
	mLayers.clear();
	for (auto& tileset : mTilesets)
	{
		SDL_DestroyTexture(tileset.image.texture);
		tileset.image.texture = NULL;
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

void TiledMap::renderLayer(const std::vector<uint32_t>& layer,
	const std::string& mapOrientation, int mapWidthInTiles,
	int mapHeightInTiles, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	if (mapOrientation == "orthogonal")
	{
		renderOrthogonalLayer(layer, mapWidthInTiles, mapHeightInTiles,
			tileWidth, tileHeight, renderer);
	}
	else if (mapOrientation == "isometric")
	{
		renderIsometricLayer(layer, mapWidthInTiles, mapHeightInTiles,
			tileWidth, tileHeight, renderer);
	}
	else if (mapOrientation == "staggered")
	{
		renderStaggeredLayer(layer, mapWidthInTiles, mapHeightInTiles,
			tileWidth, tileHeight, renderer);
	}
}

void TiledMap::renderOrthogonalLayer(const std::vector<uint32_t>& layer,
	int mapWidthInTiles, int mapHeightInTiles, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	const int mapWidth = mapWidthInTiles * tileWidth;
	int x = 0;
	int y = 0;

	for (auto gid : layer)
	{
		const Tileset* tileset = findTileset(mTilesets, gid);
		if (tileset)
		{
			renderTile(x, y, *tileset, gid, tileWidth, tileHeight, renderer);
		}

		x += tileWidth;
		if (x >= mapWidth)
		{
			x = 0;
			y += tileHeight;
		}
	}
}

void TiledMap::renderIsometricLayer(const std::vector<uint32_t>& layer,
	int mapWidthInTiles, int mapHeightInTiles, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	const int halfTileWidth = tileWidth / 2;
	const int halfTileHeight = tileHeight / 2;
	int x = (mapHeightInTiles - 1) * halfTileWidth;
	int y = 0;
	const uint32_t* pGid = &layer[0];

	for (int j = 0; j < mapHeightInTiles; ++j)
	{
		for (int i = 0; i < mapWidthInTiles; ++i)
		{
			const Tileset* tileset = findTileset(mTilesets, *pGid);
			if (tileset)
			{
				renderTile(x, y, *tileset, *pGid, tileWidth, tileHeight,
					renderer);
			}

			x += halfTileWidth;
			y += halfTileHeight;
			++pGid;
		}

		x -= (mapWidthInTiles + 1) * halfTileWidth;
		y -= (mapWidthInTiles - 1) * halfTileHeight;
	}
}

void TiledMap::renderStaggeredLayer(const std::vector<uint32_t>& layer,
	int mapWidthInTiles, int mapHeightInTiles, int tileWidth, int tileHeight,
	SDL_Renderer* renderer)
{
	// TODO
}

const TiledMap::Tileset* TiledMap::findTileset(
	const std::vector<Tileset>& tilesets, uint32_t gid) const
{
	const Tileset* tileset = NULL;

	for (size_t i = tilesets.size(); i > 0; --i)
	{
		if (gid >= tilesets[i - 1].firstgid)
		{
			// found tileset
			tileset = &tilesets[i - 1];
			break;
		}
	}

	return tileset;
}

void TiledMap::renderTile(int x, int y, const TiledMap::Tileset& tileset,
	uint32_t gid, int tileWidth, int tileHeight, SDL_Renderer* renderer)
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

			SDL_RenderCopy(renderer, tileset.image.texture, &srcRect,
				&dstRect);

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
	Tileset tileset;

	if (tilesetElement->QueryUnsignedAttribute("firstgid", &tileset.firstgid)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset firstgid attribute missing\n";
		return false;
	}

	int tilesetTileWidth = 0;
	if (tilesetElement->QueryIntAttribute("tilewidth", &tilesetTileWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tilewidth attribute missing\n";
		return false;
	}

	int tilesetTileHeight = 0;
	if (tilesetElement->QueryIntAttribute("tileheight", &tilesetTileHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tileheight attribute missing\n";
		return false;
	}

	if (tilesetTileWidth != tileWidth || tilesetTileHeight != tileHeight)
	{
		std::cerr << "map/tileset tile size difference not supported\n";
		return false;
	}

	const tinyxml2::XMLElement* imageElement =
		tilesetElement->FirstChildElement("image");

	if (!imageElement)
	{
		std::cerr << "image element not found\n";
		return false;
	}

	const char* imageSource = imageElement->Attribute("source");
	if (!imageSource)
	{
		std::cerr << "image source attribute not found\n";
		return false;
	}
	tileset.image.source = imageSource;

	if (imageElement->QueryIntAttribute("width", &tileset.image.width)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "image width attribute missing\n";
		return false;
	}

	if (imageElement->QueryIntAttribute("height", &tileset.image.height)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "image height attribute missing\n";
		return false;
	}

	tileset.image.texture = createTexture(dir, tileset.image.source,
		renderer);
	if (!tileset.image.texture)
	{
		std::cerr << "could not create image texture\n";
		return false;
	}

	mTilesets.push_back(tileset);

	return true;
}

SDL_Texture* TiledMap::createTexture(const std::string& dir,
	const std::string& imageSource, SDL_Renderer* renderer)
{
	std::string path = dir + imageSource;
	SDL_Surface* surface = IMG_Load(path.c_str());
	if (!surface)
	{
		std::cerr << "load image failed\n";
		return NULL;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_FreeSurface(surface);
	surface = NULL;

	if (!texture)
	{
		std::cerr << "create texture from surface failed\n";
		return NULL;
	}

	return texture;
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
	const tinyxml2::XMLElement* dataElement =
		layerElement->FirstChildElement("data");

	if (!dataElement)
	{
		std::cerr << "data element not found\n";
		return false;
	}

	std::vector<uint32_t> layer;

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
			layer.push_back(gid);

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

		layer.resize(mapWidthInTiles * mapHeightInTiles);
		unsigned long destLen = layer.size() * sizeof(uint32_t);
		if (uncompress((unsigned char*)layer.data(), &destLen,
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

//////////////////////////////////////////////////////////////////////////////

TiledMap gTiledMap;

bool loadAssets(SDL_Renderer* renderer)
{
	if (!gTiledMap.load(mapDir, mapName, renderer))
	{
		std::cerr << "Load tile map failed\n";
		return false;
	}

	return true;
}

void destroyAssets()
{
	gTiledMap.close();
}

int main(int argc, char* argv[])
{
	int retVal = 0;

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
							}

							// Perform game logic here

							// Do rendering
							SDL_SetRenderDrawColor(renderer, 128, 128, 128,
								255);
							SDL_RenderClear(renderer);
							gTiledMap.render(renderer);
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
		std::cerr << "SDL_Init failed, error = " << SDL_GetError() << "\n";
		retVal = 1;
	}

	return retVal;
}
