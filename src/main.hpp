#include <string>
#include <chrono>
#include <memory>
#include "ImFrame/Include/ImFrame.h"
#include "rapidjson.h"
#include "rapidjson/document.h"
#include <filesystem>

namespace json = rapidjson;

class AppSettings {
public:
	AppSettings();
	std::string poolKey;
	std::string farmKey;
	std::string finalDir; 
	std::string tempDir; 
	std::string tempDir2; 
	uint8_t ksize {32};
	uint32_t buckets {128};
	uint32_t stripes {65536};
	uint8_t threads {2};
	uint32_t buffer {4096};
	bool bitfield {true};

	static uint8_t defaultKSize;
	static uint32_t defaultBuckets;
	static uint32_t defaultStripes;
	static uint32_t defaultBuffer;

	void load();
	void load(std::filesystem::path f);
	void save(std::filesystem::path f);
protected:	
};

class MainApp : public ImFrame::ImApp
{
public:
	MainApp(GLFWwindow * window);
	virtual ~MainApp() {}
	void OnUpdate() override;
	static AppSettings settings;
protected:
	int activeTab {0};
	void toolPage();
	void statPage();
	void systemPage();
	void helpPage();
	void settingsPage();
	int wx,wy,ww,wh;	
};