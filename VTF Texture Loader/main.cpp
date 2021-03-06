#include "../SDK/MemWriter.h"
#include "DLLExports.h"
#include "VTFLib/VTFLib.h"
#include "VKFormat.h"

using namespace GMFSDK;

//DLL entry point function
#ifdef _WIN32
BOOL WINAPI DllMain(_In_ HINSTANCE hinstDLL, _In_ DWORD fdwReason, _In_ LPVOID lpvReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		vlInitialize();
		break;
	case DLL_PROCESS_DETACH:
		vlShutdown();
		break;
	}
	return TRUE;
}
#else
int main(int argc, const char* argv[])
{
	return 0;
}
#endif

//Returns all plugin information in a JSON string
int GetPluginInfo(unsigned char* cs, int maxsize)
{
	std::string s =
	"{"
		"\"turboPlugin\":{"
			"\"title\":\"Valve texture Loader.\","
			"\"description\":\"Load Valve texture format image files.\","
			"\"author\":\"Josh Klint\","
			"\"url\":\"www.turboengine.com\","
			"\"extension\": [\"vtf\"],"
			"\"filter\": [\"Valve Texture Format (*.vtf):vtf\"]"
		"}"
	"}";

	if (maxsize > 0) memcpy(cs, s.c_str(), min(maxsize,s.length() ) );
	return s.length();
}

MemWriter* writer = nullptr;
std::vector<unsigned char> LoadData;

//Texture load function
void* LoadTexture(void* data, uint64_t size, wchar_t* cpath, uint64_t& size_out)
{
	vlUInt img = 0;
	const int MAGIC_VTF = 4609110;

	if (size < 4) return nullptr;

	//Check file type
	int vtftag;
	memcpy(&vtftag, data, 4);
	if (vtftag != MAGIC_VTF) return nullptr;
	
	auto succ = vlCreateImage(&img);
	if (succ == false)
	{
		printf(vlGetLastError());
		return nullptr;
	}

	vlBindImage(img);

	auto bound = vlImageIsBound();

	if (vlImageLoadLump(data, size, false) == false)
	{
		printf(vlGetLastError());
		return nullptr;
	}

	int width = vlImageGetWidth();
	int height = vlImageGetHeight();
	int depth = vlImageGetDepth();
	int format = 0;
	int target = 2;// 2D texture
	bool convert = false;

	auto vtfformat = vlImageGetFormat();

	switch (vtfformat)
	{
	case IMAGE_FORMAT_RGBA8888:
		format = VK_FORMAT_R8G8B8A8_UNORM;	
	case IMAGE_FORMAT_BGRA8888:
		format = VK_FORMAT_B8G8R8A8_UNORM;
		break;
	case IMAGE_FORMAT_DXT1:
		format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		break;
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
		format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		break;
	case IMAGE_FORMAT_DXT3:
		format = VK_FORMAT_BC2_UNORM_BLOCK;
		break;
	case IMAGE_FORMAT_DXT5:
		format = VK_FORMAT_BC3_UNORM_BLOCK;
		break;
	default:
		format = VK_FORMAT_R8G8B8A8_UNORM;
		convert = true;
		break;
	}

	if (depth > 1) target = 3;// 3D texture
	int faces = vlImageGetFaceCount();
	if (faces != 1 && faces != 6)
	{
		vlImageDestroy();
		return nullptr;
	}
	if (faces == 6)
	{
		target = 4;// cubemap
		if (depth > 1)
		{
			vlImageDestroy();
			return nullptr;
		}
	}

	int lods = vlImageGetMipmapCount();
	
	writer = new MemWriter;
	std::string tag = "GTF2";
	int version = 200;
	writer->Write((void*)tag.c_str(), 4);
	writer->Write(&version);
	writer->Write(&format);
	writer->Write(&target);
	writer->Write(&width);
	writer->Write(&height);
	writer->Write(&depth);
	writer->Write(&lods);

	for (int f = 0; f < faces; ++f)
	{
		for (int n = 0; n < lods; ++n)
		{
			writer->Write(&width);
			writer->Write(&height);
			writer->Write(&depth);
			
			uint64_t mipsize = vlImageComputeMipmapSize(width, height, depth, 0, vlImageGetFormat());
			if (convert) mipsize = width * height * 4;
			writer->Write(&mipsize);

			for (int z = 0; z < depth; ++z)
			{
				auto pixels = vlImageGetData(0, f, z, n);
				if (convert)
				{
					auto datasize = LoadData.size();
					LoadData.resize(datasize + mipsize);
					vlImageConvertToRGBA8888(pixels, LoadData.data() + datasize, width, height, vlImageGetFormat());
					pixels = LoadData.data() + datasize;
				}
				//writer->Write(pixels, mipsize);
				writer->Write(&pixels, sizeof(void*));
			}

			if (width > 1) width /= 2;
			if (height > 1) height /= 2;
			if (depth > 1) depth /= 2;
		}
	}

	size_out = writer->Size();
	return writer->data();
}

void Cleanup()
{
	delete writer;
	writer = nullptr;
	LoadData.clear();
	vlImageDestroy();
}