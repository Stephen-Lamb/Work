#include <iostream>
#include <vector>
#include "base64.h"
#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_image.h"
#include "tinyxml2.h"
#include "zlib.h"

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
		int mapWidth, int mapHeight, int mapTileWidth, int mapTileHeight,
		SDL_Renderer* renderer);
	void renderTile(int x, int y, const Tileset& tileset, uint32_t gid,
		int mapTileWidth, int mapTileHeight, SDL_Renderer* renderer);

	bool readAllTilesets(tinyxml2::XMLConstHandle mapHandle,
		const std::string& dir, int mapTileWidth, int mapTileHeight,
		SDL_Renderer* renderer);
	bool readTileset(const tinyxml2::XMLElement* tilesetElement,
		const std::string& dir, int mapTileWidth, int mapTileHeight,
		SDL_Renderer* renderer);
	SDL_Texture* createTexture(const std::string& dir,
		const std::string& imageSource, SDL_Renderer* renderer);

	bool readAllLayers(tinyxml2::XMLConstHandle mapHandle,
		int mapWidth, int mapHeight);
	bool readLayer(const tinyxml2::XMLElement* layerElement,
		int mapWidth, int mapHeight);

	std::string mDir;
	int mMapWidth;
	int mMapHeight;
	int mMapTileWidth;
	int mMapTileHeight;
	std::vector<Tileset> mTilesets;
	std::vector<std::vector<uint32_t> > mLayers;
};

TiledMap::TiledMap()
	: mMapWidth{ 0 }, mMapHeight{ 0 }, mMapTileWidth{ 0 }, mMapTileHeight{ 0 }
{
}

TiledMap::~TiledMap()
{
	close();
}

bool TiledMap::load(const std::string& dir, const std::string& fileName,
	SDL_Renderer* renderer)
{
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

	if (mapElement->QueryIntAttribute("width", &mMapWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map width attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("height", &mMapHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map height attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("tilewidth", &mMapTileWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map tile width attribute missing\n";
		close();
		return false;
	}

	if (mapElement->QueryIntAttribute("tileheight", &mMapTileHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "map tile height attribute missing\n";
		close();
		return false;
	}

	tinyxml2::XMLConstHandle mapHandle = mapElement;

	if (!readAllTilesets(mapHandle, mDir, mMapTileWidth, mMapTileHeight,
		renderer))
	{
		std::cerr << "read all tilesets failed\n";
		close();
		return false;
	}

	if (!readAllLayers(mapHandle, mMapWidth, mMapHeight))
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
	mMapTileHeight = 0;
	mMapTileWidth = 0;
	mMapHeight = 0;
	mMapWidth = 0;
	mDir = "";
}

void TiledMap::render(SDL_Renderer* renderer)
{
	for (const auto& layer : mLayers)
	{
		renderLayer(layer, mMapWidth, mMapHeight,
			mMapTileWidth, mMapTileHeight, renderer);
	}
}

void TiledMap::renderLayer(const std::vector<uint32_t>& layer,
	int mapWidth, int mapHeight, int mapTileWidth, int mapTileHeight,
	SDL_Renderer* renderer)
{
	int x = 0;
	int y = 0;

	for (auto gid : layer)
	{
		// Look for tileset that contains the gid
		for (size_t i = mTilesets.size(); i > 0; --i)
		{
			if (gid >= mTilesets[i - 1].firstgid)
			{
				// found tileset
				renderTile(x, y, mTilesets[i - 1], gid, mapTileWidth,
					mapTileHeight, renderer);

				break;
			}
		}

		x += mapTileWidth;
		if (x >= mapWidth * mapTileWidth)
		{
			x = 0;
			y += mapTileHeight;
		}
	}
}

void TiledMap::renderTile(int x, int y, const TiledMap::Tileset& tileset,
	uint32_t gid, int mapTileWidth, int mapTileHeight, SDL_Renderer* renderer)
{
	int imageX = 0;
	int imageY = 0;

	int tilesAcross = tileset.image.width / mapTileWidth;
	int numTiles = tilesAcross * (tileset.image.height / mapTileHeight);

	// Look for tile in tileset
	for (int i = 0; i < numTiles; ++i)
	{
		if (gid == tileset.firstgid + i)
		{
			// found tile
			SDL_Rect srcRect;
			srcRect.x = imageX;
			srcRect.y = imageY;
			srcRect.w = mapTileWidth;
			srcRect.h = mapTileHeight;

			SDL_Rect dstRect;
			dstRect.x = x;
			dstRect.y = y;
			dstRect.w = mapTileWidth;
			dstRect.h = mapTileHeight;

			SDL_RenderCopy(renderer, tileset.image.texture, &srcRect,
				&dstRect);

			break;
		}

		imageX += mapTileWidth;
		if (imageX >= tilesAcross * mapTileWidth)
		{
			imageX = 0;
			imageY += mapTileHeight;
		}
	}
}

bool TiledMap::readAllTilesets(tinyxml2::XMLConstHandle mapHandle,
	const std::string& dir, int mapTileWidth, int mapTileHeight,
	SDL_Renderer* renderer)
{
	const tinyxml2::XMLElement* tilesetElement =
		mapHandle.FirstChildElement("tileset").ToElement();

	while (tilesetElement)
	{
		if (!readTileset(tilesetElement, dir, mapTileWidth, mapTileHeight,
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
	const std::string& dir, int mapTileWidth, int mapTileHeight,
	SDL_Renderer* renderer)
{
	Tileset tileset;

	if (tilesetElement->QueryUnsignedAttribute("firstgid", &tileset.firstgid)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset firstgid attribute missing\n";
		return false;
	}

	int tileWidth = 0;
	if (tilesetElement->QueryIntAttribute("tilewidth", &tileWidth)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tilewidth attribute missing\n";
		return false;
	}

	int tileHeight = 0;
	if (tilesetElement->QueryIntAttribute("tileheight", &tileHeight)
		!= tinyxml2::XML_NO_ERROR)
	{
		std::cerr << "tileset tileheight attribute missing\n";
		return false;
	}

	if (tileWidth != mapTileWidth || tileHeight != mapTileHeight)
	{
		std::cerr << "map tile width/height to tile width/height "
			"difference not supported\n";
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
	int mapWidth, int mapHeight)
{
	const tinyxml2::XMLElement* layerElement =
		mapHandle.FirstChildElement("layer").ToElement();

	while (layerElement)
	{
		if (!readLayer(layerElement, mapWidth, mapHeight))
		{
			std::cerr << "read layer failed\n";
			return false;
		}

		layerElement = layerElement->NextSiblingElement("layer");
	}

	return true;
}

bool TiledMap::readLayer(const tinyxml2::XMLElement* layerElement,
	int mapWidth, int mapHeight)
{
	const tinyxml2::XMLElement* dataElement =
		layerElement->FirstChildElement("data");

	if (!dataElement)
	{
		std::cerr << "data element not found\n";
		return false;
	}

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
		std::vector<uint32_t> layer(mapWidth * mapHeight);

		unsigned long destLen = layer.size() * sizeof(uint32_t);
		if (uncompress((unsigned char*)layer.data(), &destLen,
			(const unsigned char*)decodedText.c_str(), decodedText.length())
			!= Z_OK || destLen != (mapWidth * mapHeight * sizeof(uint32_t)))
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

		mLayers.push_back(layer);
	}
	else
	{
		std::cerr << "encoding type not supported\n";
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////

TiledMap gTiledMap;

bool loadAssets(SDL_Renderer* renderer)
{
	if (!gTiledMap.load("tileset\\", "myattempt.tmx", renderer))
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
			SDL_Window* window = SDL_CreateWindow("Tiled Intro",
				SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
				640, 640, 0);

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
							SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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
