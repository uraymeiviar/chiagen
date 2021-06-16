#include <algorithm>
#include <iostream>
#include <fstream>
#include <clocale>
#include <locale>
#include <codecvt>
#include <stdexcept>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include "uint128_t/uint128_t.h"

#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include "Imgui/imgui.h"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include "cli.hpp"
#include "main.hpp"
#include "chiapos/pos_constants.hpp"
#include "chiapos/entry_sizes.hpp"

#include "JobCreatePlot.h"


extern "C" {
#include "glfw/include/GLFW/glfw3.h"
}

#ifdef IMFRAME_WINDOWS
#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <shellapi.h>
#endif

AppSettings MainApp::settings = AppSettings();
static const WORD MAX_CONSOLE_LINES = 500;
bool RedirectIOToConsole()
{
	bool result = true;
	FILE* fp;

	// Redirect STDIN if the console has an input handle
	if (GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE)
		if (freopen_s(&fp, "CONIN$", "r", stdin) != 0)
			result = false;
		else
			setvbuf(stdin, NULL, _IONBF, 0);

	// Redirect STDOUT if the console has an output handle
	if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE)
		if (freopen_s(&fp, "CONOUT$", "w", stdout) != 0)
			result = false;
		else
			setvbuf(stdout, NULL, _IONBF, 0);

	// Redirect STDERR if the console has an error handle
	if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
		if (freopen_s(&fp, "CONOUT$", "w", stderr) != 0)
			result = false;
		else
			setvbuf(stderr, NULL, _IONBF, 0);

	// Make C++ standard streams point to console as well.
	std::ios::sync_with_stdio(true);

	// Clear the error state for each of the C++ standard streams.
	std::wcout.clear();
	std::cout.clear();
	std::wcerr.clear();
	std::cerr.clear();
	std::wcin.clear();
	std::cin.clear();

	return result;
}

bool ReleaseConsole()
{
	bool result = true;
	FILE* fp;

	// Just to be safe, redirect standard IO to NUL before releasing.

	// Redirect STDIN to NUL
	if (freopen_s(&fp, "NUL:", "r", stdin) != 0)
		result = false;
	else
		setvbuf(stdin, NULL, _IONBF, 0);

	// Redirect STDOUT to NUL
	if (freopen_s(&fp, "NUL:", "w", stdout) != 0)
		result = false;
	else
		setvbuf(stdout, NULL, _IONBF, 0);

	// Redirect STDERR to NUL
	if (freopen_s(&fp, "NUL:", "w", stderr) != 0)
		result = false;
	else
		setvbuf(stderr, NULL, _IONBF, 0);

	// Detach from console
	if (!FreeConsole())
		result = false;

	return result;
}

void AdjustConsoleBuffer(int16_t minLength)
{
	// Set the screen buffer to be big enough to scroll some text
	CONSOLE_SCREEN_BUFFER_INFO conInfo;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &conInfo);
	if (conInfo.dwSize.Y < minLength)
		conInfo.dwSize.Y = minLength;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), conInfo.dwSize);
}

bool CreateNewConsole(int16_t minLength)
{
	bool result = false;

	// Release any current console and redirect IO to NUL
	ReleaseConsole();

	// Attempt to create new console
	if (AllocConsole())
	{
		AdjustConsoleBuffer(minLength);
		result = RedirectIOToConsole();
	}

	return result;
}

void my_terminate() {
    std::cout << "terminate called " << std::endl;
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd) {
#ifdef _WIN32	
	// the following line increases the number of open simultaneous files
	int newmaxstdio = _setmaxstdio(8192);
#endif
	std::set_terminate(my_terminate);

	LPWSTR *args;
	int nArgs;
	JobManager::getInstance().start();
	args = CommandLineToArgvW(GetCommandLineW(), &nArgs);

	std::filesystem::path settingsPath = std::filesystem::current_path() / "settings.json";
	if (std::filesystem::exists(settingsPath)) {
		try {
			MainApp::settings.load(settingsPath);
		}
		catch(...){}
	}
	if (nArgs < 2) {
		ImFrame::Run("uraymeiviar", "Chia Plotter", [] (const auto & params) { 
			return std::make_unique<MainApp>(params); 
		});
	}
	else {
		AttachConsole(ATTACH_PARENT_PROCESS );
		HWND consoleWnd = GetConsoleWindow();
		bool ownConsole = false;
		if (consoleWnd == NULL)
		{
			ownConsole = CreateNewConsole(1024);
		}
		else {
			RedirectIOToConsole();
		}
		std::cout << std::endl;
		if (nArgs >= 1)
		{
			std::filesystem::path exePath(args[0]);
			if (nArgs >= 2) {
				std::wstring command = std::wstring(args[1]);
				if (lowercase(command) == L"create") {
					if (nArgs < 4) {
						std::cout << "Usage "<< exePath.filename().string() <<" create <<options>>" << std::endl;
						std::cout << "options :" << std::endl;
						std::cout << "  -f  --farmkey     : farmer public key in hex (48 bytes)" << std::endl;
						std::cout << "                      if not specified, plot id must be given" << std::endl;
						std::cout << "  -p  --poolkey     : pool public key in hex (48 bytes)" << std::endl;
						std::cout << "                      if not specified, plot id must be given" << std::endl;
						std::cout << "  -d  --dest        : plot destination path" << std::endl;
						std::cout << "                      if not specified will use current path" << std::endl;
						std::cout << "  -t  --temp        : plotting temporary path" << std::endl;
						std::cout << "                      if not specified will use current path" << std::endl;
						std::cout << "  -l  --temp2       : plotting final temporary path" << std::endl;
						std::cout << "                      if not specified will use temp path" << std::endl;
						std::cout << "  -o  --output      : output file name" << std::endl;
						std::cout << "                      if not specified will be named plot-[k]-[date]-[id].plot" << std::endl;
						std::cout << "  -z  --memo        : plot memo bytes in hex" << std::endl;
						std::cout << "                      if not specified will generated from farm and pool public key" << std::endl;
						std::cout << "  -i  --id          : plot id in hex" << std::endl;
						std::cout << "                      if specified farmkey and pool key will be ignored" << std::endl;
						std::cout << "  -k  --size        : plot size               (default 32   )" << std::endl;
						std::cout << "  -r  --threads     : cpu threads to use      (default 2    )" << std::endl;
						std::cout << "  -b  --buckets     : number of buckets       (default 128  )" << std::endl;
						std::cout << "  -m  --mem         : max memory buffer in MB (default 4608 )" << std::endl;
						std::cout << "  -s  --stripes     : stripes count           (default 65536)" << std::endl;
						std::cout << "  -n  --no-bitfield : use bitfield            (default set  )" << std::endl;
						std::cout << "  -w  --plotter     : madmax | chiapos        (default madmax)" << std::endl << std::endl;
						std::cout << " common usage example :" << std::endl;
						std::cout << exePath.filename().string() << " create -f b6cce9c6ff637f1dc9726f5db64776096fdb4101d673afc4e27ec71f0f9a859b2f1d661c92f3b8e6932a3f7634bc4c12 -p 86e2a9cf0b409c8ca7258f03ef7698565658a17f6f7dd9e9b0ac9be6ca3891ac09fa8468951f24879c00870e88fa66bb -d D:\\chia-plots -t C:\\chia-temp" << std::endl << std::endl;
						std::cout << "this command will create default 100GB k-32 plot to D:\\chia-plots\\ and use C:\\chia-plots as temporary directory, plot id, memo, and filename will be generated from farm and plot public key, its recommend to use buckets, k-size and stripes to default value" << std::endl;
					}
					else {
						std::string farmkey;
						std::string poolkey;
						std::wstring dest;
						std::wstring temp;
						std::wstring temp2;
						std::wstring filename;
						std::string memo;
						std::string id;
						int ksize = 32;
						int nthreads = 2;
						int buckets = 128;
						int mem = 3390;
						int stripes = 65556;
						bool useMadMax = true;
						bool bitfield = true;

						std::string lastArg = "";
						for (int i = 2; i < nArgs; i++) {
							if (lastArg.empty()) {
								lastArg = lowercase(ws2s(std::wstring(args[i])));
								if (lastArg == "-n" || lastArg == "--no-bitfield") {
									bitfield = false;
								}
							}
							else {
								if (lastArg == "-f" || lastArg == "--farmkey") {
									farmkey = lowercase(ws2s(std::wstring(args[i])));
									lastArg = "";
								}
								else if (lastArg == "-p" || lastArg == "--poolkey") {
									poolkey = lowercase(ws2s(std::wstring(args[i])));
									lastArg = "";
								}
								else if (lastArg == "-d" || lastArg == "--dest") {
									dest = std::wstring(args[i]);
									lastArg = "";
								}
								else if (lastArg == "-t" || lastArg == "--temp") {
									temp = std::wstring(args[i]);
									lastArg = "";
								}
								else if (lastArg == "-l" || lastArg == "--temp2") {
									temp2 = std::wstring(args[i]);
									lastArg = "";
								}
								else if (lastArg == "-o" || lastArg == "--output") {
									filename = std::wstring(args[i]);
									lastArg = "";
								}
								else if (lastArg == "-z" || lastArg == "--memo") {
									memo = lowercase(ws2s(std::wstring(args[i])));
									lastArg = "";
								}
								else if (lastArg == "-i" || lastArg == "--id") {
									id = lowercase(ws2s(std::wstring(args[i])));
									lastArg = "";
								}
								else if (lastArg == "-k" || lastArg == "--size") {
									std::wstring ksizeStr = std::wstring(args[i]);
									try {
										ksize = std::stoi(ksizeStr);
									}
									catch (...) {
										std::cout << "parsing error on plot size argument, revert back to default 32" << std::endl;
										ksize = 32;
									}
									lastArg = "";
								}
								else if (lastArg == "-r" || lastArg == "--threads") {
									std::wstring valStr = std::wstring(args[i]);
									try {
										nthreads = std::stoi(valStr);
									}
									catch (...) {
										std::cout << "parsing error on thread count argument, revert back to default 2" << std::endl;;
										nthreads = 2;
									}
									lastArg = "";
								}
								else if (lastArg == "-b" || lastArg == "--buckets") {
									std::wstring valStr = std::wstring(args[i]);
									try {
										buckets = std::stoi(valStr);
									}
									catch (...) {
										std::cout << "parsing error on buckets count argument, revert back to default 128" << std::endl;;
										buckets = 128;
									}
									lastArg = "";
								}
								else if (lastArg == "-m" || lastArg == "--mem") {
									std::wstring valStr = std::wstring(args[i]);
									try {
										mem = std::stoi(valStr);
									}
									catch (...) {
										std::cout << "parsing error memory size argument, revert back to default 3390 MB" << std::endl;;
										mem = 3390;
									}
									lastArg = "";
								}
								else if (lastArg == "-s" || lastArg == "--stripes") {
									std::wstring valStr = std::wstring(args[i]);
									try {
										stripes = std::stoi(valStr);
									}
									catch (...) {
										std::cout << "parsing error stripes count argument, revert back to default 65536" << std::endl;;
										stripes = 65536;
									}
									lastArg = "";
								}
								else if (lastArg == "-n" || lastArg == "--no-bitfield") {
									std::wstring valStr = std::wstring(args[i]);
									if (valStr == L"1" || lowercase(valStr) == L"true" || lowercase(valStr) == L"on") {
										bitfield = true;
									}
									else {
										bitfield = false;
									}
									lastArg = "";
								}
								else if (lastArg == "-w" || lastArg == "--plotter") {
									std::wstring valStr = std::wstring(args[i]);
									if (lowercase(valStr) == L"chiapos") {
										useMadMax = false;
									}
								}
								else {
									std::wcout << L"ignored unknown argument " << args[i] << std::endl;
									lastArg = "";
								}
							}
						}

						if (farmkey.empty() && id.empty()) {
							std::cerr << "error: either farmkey or id need to be specified";
						}
						if (poolkey.empty() && id.empty()) {
							std::cerr << "error: either poolkey or id need to be specified";
						}
						if (dest.empty()) {
							std::cout << "destination path will be using current directory" << std::endl;
						}
						else {
							std::wcout << L"destination path = " << dest << std::endl;
						}
						if (temp.empty()) {
							std::cout << "temp path will be using current directory" << std::endl;
						}
						else {
							std::wcout << L"temp path = " << temp << std::endl;
						}
						if (temp2.empty()) {
							std::cout << "temp2 path will use temp path" << std::endl;
							temp2 = temp;
						}
						else {
							std::wcout << L"temp2 path = " << temp2 << std::endl;
						}
						if (filename.empty()) {
							std::cout << "will use default plot filename" << std::endl;
						}
						else {
							std::wcout << L"plot file name = " << filename << std::endl;
						}
						if (memo.empty()) {
							std::cout << "will auto generate default memo for farming" << std::endl;
						}
						else {
							std::wcout << L"memo content = " << s2ws(memo) << std::endl;
						}
						if (id.empty()) {
							std::cout << "will auto generate default plot id for farming" << std::endl;
						}
						else {
							std::wcout << L"plot id = " << s2ws(id) << std::endl;
						}
						if (bitfield) {
							std::cout << "will generate plot with bitfield" << std::endl;
						}
						else {
							std::cout << "will generate plot without bitfield" << std::endl;
						}

						std::cout << "plot ksize    = " << std::to_string(ksize) << std::endl;
						std::cout << "threads count = " << std::to_string(nthreads) << std::endl;
						std::cout << "buckets count = " << std::to_string(buckets) << std::endl;
						std::cout << "max memsize   = " << std::to_string(mem) << " MB" << std::endl;
						std::cout << "stripes count = " << std::to_string(stripes) << std::endl;

						try {
							if (useMadMax) {
								cli_create_mad(farmkey,poolkey,dest,temp,temp2,buckets,nthreads);
							}
							else {
								cli_create(farmkey,poolkey,dest,temp,temp2,filename,memo,id,ksize,buckets,stripes,nthreads,mem,!bitfield);
							}
						}
						catch (...) {
							return 0;
						}
					}
				}
				else if (lowercase(command) == L"proof") {
					if (nArgs < 4) {
						std::cout << "Usage "<< exePath.filename().string() <<" proof <filepath> <challenge>" << std::endl;
						std::cout << "   filepath  : path to file to check" << std::endl;
						std::cout << "   challenge : multiple of 8 bytes in hex" << std::endl;
					}
					else {
						std::filesystem::path targetPath(args[2]);
						if (std::filesystem::exists(targetPath) && std::filesystem::is_regular_file(targetPath)) {
							std::string proof = ws2s(std::wstring(args[3]));
							cli_proof(proof,targetPath.wstring());
						}
						else {
							std::cerr << "file not exist" << std::endl;
						}
					}
				}
				else if (lowercase(command) == L"verify") {
					if (nArgs < 5) {
						std::cout << "Usage "<< exePath.filename().string() <<" verify <id> <proof> <challenge>" << std::endl;
						std::cout << "   id        : 32 bytes ID in hex" << std::endl;
						std::cout << "   proof     : 32 bytes proof in hex" << std::endl;
						std::cout << "   challenge : multiple of 8 bytes in hex" << std::endl;
					}
					else {
						std::string id = ws2s(std::wstring(args[2]));
						std::string proof = ws2s(std::wstring(args[3]));
						std::string challenge = ws2s(std::wstring(args[4]));
						cli_verify(id, proof, challenge);
					}
				}
				else if (lowercase(command) == L"check") {
					if (nArgs < 3) {
						std::cout << "Usage "<< exePath.filename().string() <<" check <filepath> [iteration]" << std::endl;
						std::cout << "   filepath  : path to file to check" << std::endl;
						std::cout << "   iteration : number of iteration to perform (default:100)" << std::endl;
					}
					else {
						uint32_t iteration = 100;
						std::filesystem::path targetPath(args[2]);
						if (std::filesystem::exists(targetPath) && std::filesystem::is_regular_file(targetPath)) {
							if (nArgs >= 4) {
								std::wstring iterationStr = args[3];
								try {
									iteration = std::stoi(iterationStr);
								}
								catch (...) {
									iteration = 100;
								}
							}
							cli_check(iteration,targetPath.wstring());
						}
						else {
							std::cerr << "file not exist" << std::endl;
						}
					}
				}
				else {
					nArgs = 1;
				}
			}
			if (nArgs <= 1) {
				std::cout << "Usage "<< exePath.filename().string() <<" <command> <args>" << std::endl;
				std::cout << "command options:" << std::endl;
				std::cout << "    create " << std::endl;
				std::cout << "    proof " << std::endl;
				std::cout << "    verify " << std::endl;
				std::cout << "    check " << std::endl;
			}			
		}
		
		if (ownConsole) {
			ReleaseConsole();
		}
		else {
			std::cout << "done, press any key to exit" << std::endl;
			FreeConsole();
		}
	}
	JobManager::getInstance().stop();
	return 1;
}

constexpr auto ColorFromBytes = [](uint8_t r, uint8_t g, uint8_t b)
{
	return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
};

const ImVec4 bgColor           = ColorFromBytes(0, 0, 0);
const ImVec4 lightBgColor      = ColorFromBytes(40, 65, 50);
const ImVec4 veryLightBgColor  = ColorFromBytes(70, 115, 90);

const ImVec4 panelColor        = ColorFromBytes(80, 140, 110);
const ImVec4 panelHoverColor   = ColorFromBytes(30, 215, 140);
const ImVec4 panelActiveColor  = ColorFromBytes(0, 200, 120);

const ImVec4 textColor         = ColorFromBytes(240, 255, 225);
const ImVec4 textDisabledColor = ColorFromBytes(96, 96, 96);
const ImVec4 borderColor       = ColorFromBytes(30, 48, 40);

void Style()
{
	auto& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	colors[ImGuiCol_Text]                 = textColor;
	colors[ImGuiCol_TextDisabled]         = textDisabledColor;
	colors[ImGuiCol_TextSelectedBg]       = panelActiveColor;
	colors[ImGuiCol_WindowBg]             = bgColor;
	colors[ImGuiCol_ChildBg]              = bgColor;
	colors[ImGuiCol_PopupBg]              = lightBgColor;
	colors[ImGuiCol_Border]               = borderColor;
	colors[ImGuiCol_BorderShadow]         = borderColor;
	colors[ImGuiCol_FrameBg]              = lightBgColor;
	colors[ImGuiCol_FrameBgHovered]       = panelHoverColor;
	colors[ImGuiCol_FrameBgActive]        = panelActiveColor;
	colors[ImGuiCol_TitleBg]              = bgColor;
	colors[ImGuiCol_TitleBgActive]        = bgColor;
	colors[ImGuiCol_TitleBgCollapsed]     = bgColor;
	colors[ImGuiCol_MenuBarBg]            = panelColor;
	colors[ImGuiCol_ScrollbarBg]          = ColorFromBytes(16,16,16);
	colors[ImGuiCol_ScrollbarGrab]        = lightBgColor;
	colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
	colors[ImGuiCol_ScrollbarGrabActive]  = panelActiveColor;
	colors[ImGuiCol_CheckMark]            = textColor;
	colors[ImGuiCol_SliderGrab]           = panelHoverColor;
	colors[ImGuiCol_SliderGrabActive]     = panelActiveColor;
	colors[ImGuiCol_Button]               = ColorFromBytes(190, 100, 20);
	colors[ImGuiCol_ButtonHovered]        = ColorFromBytes(250, 150, 45);
	colors[ImGuiCol_ButtonActive]         = panelHoverColor;
	colors[ImGuiCol_Header]               = ColorFromBytes(16,32,24);
	colors[ImGuiCol_HeaderHovered]        = veryLightBgColor;
	colors[ImGuiCol_HeaderActive]         = panelActiveColor;
	colors[ImGuiCol_Separator]            = borderColor;
	colors[ImGuiCol_SeparatorHovered]     = borderColor;
	colors[ImGuiCol_SeparatorActive]      = borderColor;
	colors[ImGuiCol_ResizeGrip]           = bgColor;
	colors[ImGuiCol_ResizeGripHovered]    = lightBgColor;
	colors[ImGuiCol_ResizeGripActive]     = veryLightBgColor;
	colors[ImGuiCol_PlotLines]            = panelActiveColor;
	colors[ImGuiCol_PlotLinesHovered]     = panelHoverColor;
	colors[ImGuiCol_PlotHistogram]        = ColorFromBytes(255, 100, 0);
	colors[ImGuiCol_PlotHistogramHovered] = panelHoverColor;
	colors[ImGuiCol_DragDropTarget]       = bgColor;
	colors[ImGuiCol_NavHighlight]         = bgColor;
	colors[ImGuiCol_Tab]                  = bgColor;
	colors[ImGuiCol_TabActive]            = lightBgColor;
	colors[ImGuiCol_TabUnfocused]         = bgColor;
	colors[ImGuiCol_TabUnfocusedActive]   = panelActiveColor;
	colors[ImGuiCol_TabHovered]           = panelHoverColor;
	colors[ImGuiCol_TableHeaderBg]        = ColorFromBytes(16,16,16);

	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 4.0f;
	style.FrameRounding     = 6.0f;
	style.GrabRounding      = 4.0f;
	style.PopupRounding     = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.TabRounding       = 0.0f;
	style.FramePadding		= ImVec2(6.0f,3.0f);
	style.WindowPadding     = ImVec2(4.0f,2.0f);
	style.ItemSpacing		= ImVec2(3.0f,6.0f);
	style.AntiAliasedLines = true;
	style.AntiAliasedFill = true;
	style.TabRounding       = 4.0f;
}

const ImGuiTableFlags tableFlag = 
	ImGuiTableFlags_Resizable|
	ImGuiTableFlags_SizingFixedFit|
	ImGuiTableFlags_NoBordersInBody|
	ImGuiTableFlags_NoBordersInBodyUntilResize;

MainApp::MainApp(GLFWwindow* window) : ImFrame::ImApp(window) {
	glfwSetWindowSize(this->GetWindow(),1024,480);
}

void MainApp::OnUpdate() {
	JobManager::getInstance().update();
	Style();
	glfwGetWindowPos(this->GetWindow(),&wx,&wy);
	glfwGetWindowSize(this->GetWindow(),&ww,&wh);
	if (ww > 80 && wh > 80) {
		ImGui::SetNextWindowPos(ImVec2((float)wx,(float)wy));
		ImGui::SetNextWindowSize(ImVec2((float)(ww),(float)(wh)));
		ImGui::SetNextWindowSizeConstraints(ImVec2((float)ww,(float)wh),ImVec2(float(ww),float(wh)));
		if (ImGui::Begin("gfg",nullptr,ImGuiWindowFlags_NoDecoration)) {
			if (ImGui::BeginTabBar("MainTab")) {
				if(ImGui::BeginTabItem("Tools")){
					this->activeTab = 0;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Stats")) {
					this->activeTab = 1;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("System")) {
					this->activeTab = 2;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Help")) {
					this->activeTab = 3;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Settings")) {
					this->activeTab = 4;
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			if (this->activeTab == 0) {
				this->toolPage();
			}
			else if (this->activeTab == 1) {
				this->statPage();
			}
			else if(this->activeTab == 2){
				this->systemPage();
			}
			else if (this->activeTab == 3) {
				this->helpPage();
			}
			else if (this->activeTab == 4) {
				this->settingsPage();
			}
			ImGui::End();
		}	
	}
}

void MainApp::toolPage() {
	if(ImGui::BeginTable("##toolTable",3,tableFlag)){
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Create Job",ImGuiTableColumnFlags_WidthFixed,340.0f);
		ImGui::TableSetupColumn("Active Job",ImGuiTableColumnFlags_WidthFixed,230.0f);
		ImGui::TableSetupColumn("Job Status",ImGuiTableColumnFlags_WidthStretch,240);
		ImGui::TableHeadersRow();			

		ImGui::TableNextRow();
		if (ImGui::TableSetColumnIndex(0)) {
			if (ImGui::BeginChild("##col1", ImVec2(0.0f, 0.0f),false,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
				JobManager& jm = JobManager::getInstance();
				for (auto jobFactory : jm.jobFactories) {
					if (ImGui::CollapsingHeader(jobFactory->getName().c_str())) {
						ImGui::Indent(20.0f);
						ImGui::PushID((const void*)jobFactory.get());
						jobFactory->drawEditor();
						ImGui::PopID();
						ImGui::Unindent(20.0f);
					}
				}
				if (ImGui::CollapsingHeader("Check Plot")) {
					ImGui::Text("Under development");
				}					
			}
			ImGui::EndChild();
		}

		if (ImGui::TableSetColumnIndex(1)) {
			if (JobManager::getInstance().countJob() < 1) {
				ImGui::Text("No Active Job, create from left panel");
			}
			else {
				if (ImGui::BeginChild("##col2", ImVec2(0.0f, 0.0f),false,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
					//TODO:lock job manager mutex
					//auto lock = JobManager::getInstance().lock();
					for(auto it = JobManager::getInstance().jobIteratorBegin() ; it != JobManager::getInstance().jobIteratorEnd() ; it++){
						ImGui::PushID((const void*)it->get());
						ImVec2 cursorBegin = ImGui::GetCursorPos();
						if (*it == JobManager::getInstance().getSelectedJob()) {
							ImGui::GetStyle().Colors[ImGuiCol_Border] = textColor;
						}
						else {
							ImGui::GetStyle().Colors[ImGuiCol_Border] = lightBgColor;
						}
						ImGui::BeginGroupPanel();
						(*it)->drawItemWidget();
						ImVec2 cursorEnd = ImGui::GetCursorPos();
						float invisWidth = ImGui::GetContentRegionAvailWidth();
						ImGui::SetCursorPos(cursorBegin);
						if(ImGui::InvisibleButton("select", ImVec2(ImGui::GetContentRegionAvailWidth(), cursorEnd.y - cursorBegin.y))){
							JobManager::getInstance().setSelectedJob(*it);
						}					
						ImGui::EndGroupPanel();
						ImGui::GetStyle().Colors[ImGuiCol_Border] = borderColor;
						ImGui::PopID();
					}
					//lock.unlock();
				}
				ImGui::EndChild();
			}
		}

		if (ImGui::TableSetColumnIndex(2)) {
			std::shared_ptr<Job> job = JobManager::getInstance().getSelectedJob();
			if (job) {
				if (ImGui::BeginChild("##col3",ImVec2(0,0),false,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
					job->drawStatusWidget();						
				}
				ImGui::EndChild();
			}			
		}
		ImGui::EndTable();
	}
}

void MainApp::statPage() {}

void MainApp::systemPage() {}

void MainApp::helpPage() {}

void MainApp::settingsPage()
{
	if(ImGui::BeginTable("##setingsTable",3,tableFlag)){
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Default Values",ImGuiTableColumnFlags_WidthFixed,340.0f);
		ImGui::TableSetupColumn("App",ImGuiTableColumnFlags_WidthStretch,230.0f);
		ImGui::TableHeadersRow();			

		ImGui::TableNextRow();
		if (ImGui::TableSetColumnIndex(0)) {
			if (ImGui::BeginChild("##settings-col1", ImVec2(0.0f, 0.0f),false,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
				float fieldWidth = ImGui::GetWindowContentRegionWidth();
				bool changed = false;

				ImGui::PushItemWidth(80.0f);
				ImGui::Text("Pool Key");
				ImGui::PopItemWidth();
				ImGui::SameLine(80.0f);
				ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
				changed |= ImGui::InputText("##settings-poolkey", &MainApp::settings.poolKey,ImGuiInputTextFlags_CharsHexadecimal);
				if (ImGui::IsItemHovered() && !MainApp::settings.poolKey.empty()) {
					ImGui::BeginTooltip();
					tooltiipText(MainApp::settings.poolKey.c_str());
					ImGui::EndTooltip();
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PushItemWidth(55.0f);
				if (ImGui::Button("Paste##pool")) {
					MainApp::settings.poolKey = strFilterHexStr(ImGui::GetClipboardText());
					changed = true;
				}
				ImGui::PopItemWidth();

				ImGui::PushItemWidth(80.0f);
				ImGui::Text("Farm Key");
				ImGui::PopItemWidth();
				ImGui::SameLine(80.0f);
				ImGui::PushItemWidth(fieldWidth-(80.0f + 55.0f));
				ImGui::InputText("##settings-farmkey", &MainApp::settings.farmKey,ImGuiInputTextFlags_CharsHexadecimal);
				if (ImGui::IsItemHovered() && !this->MainApp::settings.farmKey.empty()) {
					ImGui::BeginTooltip();
					tooltiipText(this->MainApp::settings.farmKey.c_str());
					ImGui::EndTooltip();
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PushItemWidth(55.0f);
				if (ImGui::Button("Paste##settings-farm")) {
					this->MainApp::settings.farmKey = strFilterHexStr(ImGui::GetClipboardText());
					changed |= true;
				}
				ImGui::PopItemWidth();

				ImGui::PushItemWidth(80.0f);
				ImGui::Text("Dest Dir");
				ImGui::PopItemWidth();
				ImGui::SameLine(80.0f);
				static std::string destDir = MainApp::settings.finalDir;
				ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
				if (ImGui::InputText("##settings-destDir", &destDir)) {
					MainApp::settings.finalDir = destDir;
					changed |= true;
				}
				if (ImGui::IsItemHovered() && !destDir.empty()) {
					ImGui::BeginTooltip();
					tooltiipText(destDir.c_str());
					ImGui::EndTooltip();
				}
				ImGui::PopItemWidth();
				ImGui::PushItemWidth(55.0f);
				ImGui::SameLine();
				if (ImGui::Button("Paste##settings-destDir")) {
					destDir = ImGui::GetClipboardText();
					MainApp::settings.finalDir = destDir;
					changed |= true;
				}
				ImGui::PopItemWidth();
				ImGui::PushItemWidth(55.0f);
				ImGui::SameLine();
				if (ImGui::Button("Select##settings-destDir")) {
					std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
					if (dirPath) {
						destDir = dirPath.value().string();
						MainApp::settings.finalDir = destDir;
						changed |= true;
					}		
				}
				ImGui::PopItemWidth();

				ImGui::PushItemWidth(80.0f);
				ImGui::Text("Temp Dir");
				ImGui::PopItemWidth();
				ImGui::SameLine(80.0f);
				static std::string tempDir = MainApp::settings.tempDir;
				ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
				if (ImGui::InputText("##settings-tempDir", &tempDir)) {
					MainApp::settings.tempDir = tempDir;
					changed |= true;
				}
				if (ImGui::IsItemHovered() && !tempDir.empty()) {
					ImGui::BeginTooltip();
					tooltiipText(tempDir.c_str());
					ImGui::EndTooltip();
				}
				ImGui::PopItemWidth();
		
				ImGui::PushItemWidth(55.0f);
				ImGui::SameLine();		
				if (ImGui::Button("Paste##settings-tempDir")) {
					tempDir = ImGui::GetClipboardText();
					MainApp::settings.tempDir = tempDir;
					changed |= true;
				}
				ImGui::PopItemWidth();
				ImGui::PushItemWidth(55.0f);
				ImGui::SameLine();
				if (ImGui::Button("Select##settings-tempDir")) {
					std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
					if (dirPath) {
						tempDir = dirPath->string();
						MainApp::settings.tempDir = tempDir;
						changed |= true;
					}		
				}
				ImGui::PopItemWidth();

				ImGui::Text("Temp Dir2");
				ImGui::SameLine(120.0f);
				static std::string tempDir2 = MainApp::settings.tempDir2;
				ImGui::PushItemWidth(fieldWidth-225.0f);
				if (ImGui::InputText("##settings-tempDir2", &tempDir2)) {
					MainApp::settings.tempDir2 = tempDir2;
					changed |= true;
				}
				if (ImGui::IsItemHovered() && !tempDir2.empty()) {
					ImGui::BeginTooltip();
					tooltiipText(tempDir2.c_str());
					ImGui::EndTooltip();
					changed |= true;
				}
				ImGui::PopItemWidth();
				ImGui::SameLine();
				ImGui::PushItemWidth(50.0f);
				if (ImGui::Button("Paste##settings-tempDir2")) {
					tempDir2 = ImGui::GetClipboardText();
					MainApp::settings.tempDir2 = tempDir2;
					changed |= true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Select##settings-tempDir2")) {
					std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
					if (dirPath) {
						tempDir2 = dirPath.value().string();
						MainApp::settings.tempDir2 = tempDir2;
						changed |= true;
					}		
				}
				ImGui::PopItemWidth();

				ImGui::Text("Plot Size (k)");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				static int kinput = (int)MainApp::settings.ksize;
				if (ImGui::InputInt("##settings-k", &kinput, 1, 10)) {
					MainApp::settings.ksize = (uint8_t)kinput;
					if (MainApp::settings.ksize < 18) {
						MainApp::settings.ksize = 18;
					}
					if (MainApp::settings.ksize > 50) {
						MainApp::settings.ksize = 50;
					}
					kinput = (int)MainApp::settings.ksize;
					changed |= true;
				}
				ImGui::PopItemWidth();

				ImGui::Text("Buckets");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				static int bucketInput = (int)MainApp::settings.buckets;
				if (ImGui::InputInt("##settings-buckets", &bucketInput, 1, 8)) {
					MainApp::settings.buckets = (uint32_t)bucketInput;
					if (MainApp::settings.buckets < 16) {
						MainApp::settings.buckets = 16;
					}
					if (MainApp::settings.buckets > 256) {
						MainApp::settings.buckets = 256;
					}
					bucketInput= (uint32_t)this->MainApp::settings.buckets;
					changed |= true;
				}

				ImGui::Text("Stripes");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				static int stripesInput = (int)MainApp::settings.stripes;
				if (ImGui::InputInt("##settings-stripes", &stripesInput, 1024, 4096)) {
					MainApp::settings.stripes = stripesInput;
					if (MainApp::settings.stripes < 256) {
						MainApp::settings.stripes = 256;
					}
					changed |= true;
				}
				ImGui::PopItemWidth();

				ImGui::Text("Threads");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				static int threadsInput = (int)MainApp::settings.threads;
				if (ImGui::InputInt("##settings-threads", &threadsInput, 1, 2)) {
					MainApp::settings.threads = (uint8_t)threadsInput;
					if (MainApp::settings.threads < 1) {
						MainApp::settings.threads = 1;
						threadsInput = 1;
					}
					changed |= true;
				}
				ImGui::PopItemWidth();

				ImGui::Text("Buffer (MB)");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				static int bufferInput = (int)MainApp::settings.buffer;
				if (ImGui::InputInt("##settings-buffer", &bufferInput, 1024, 2048)) {
					MainApp::settings.buffer = bufferInput;
					if (MainApp::settings.buffer < 16) {
						MainApp::settings.buffer = 16;
						bufferInput = 16;
					}
					changed |= true;
				}
				ImGui::PopItemWidth();

				ImGui::Text("Bitfield");
				ImGui::SameLine(120.0f);
				ImGui::PushItemWidth(fieldWidth-130.0f);
				changed |= ImGui::Checkbox("##settings-bitfeld", &MainApp::settings.bitfield);
				ImGui::PopItemWidth();

				if (changed) {
					MainApp::settings.save(std::filesystem::current_path() / "settings.json");
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndTable();
	}
}

AppSettings::AppSettings()
{
	this->threads = std::thread::hardware_concurrency()/2;
	if (this->threads < 2) {
		this->threads = 2;
	}
}

uint8_t AppSettings::defaultKSize = 32;
uint32_t AppSettings::defaultBuckets = 256;
uint32_t AppSettings::defaultStripes = 65536;
uint32_t AppSettings::defaultBuffer = 4096;

void AppSettings::load(std::filesystem::path f)
{
	if (std::filesystem::exists(f) && std::filesystem::is_regular_file(f)) {
		std::ifstream ifs(f.string());
		if ( ifs.is_open() )
		{
			std::stringstream buffer;
			buffer << ifs.rdbuf();
			std::string inputJson = buffer.str();
			
			json::Document doc;
			doc.Parse(inputJson.c_str());
			if(doc["settings"].IsObject()){
				json::Value& settings = doc["settings"];
				if (settings.IsObject()) {
					json::Value& farmkey = settings["farmkey"];
					if (farmkey.IsString()) {
						this->farmKey = farmkey.GetString();
					}

					json::Value& poolkey = settings["poolkey"];
					if (poolkey.IsString()) {
						this->poolKey = poolkey.GetString();
					}

					json::Value& finaldir = settings["finaldir"];
					if (finaldir.IsString()) {
						this->finalDir = finaldir.GetString();
					}

					json::Value& tempdir = settings["tempdir"];
					if (tempdir.IsString()) {
						this->tempDir = tempdir.GetString();
					}

					json::Value& tempdir2 = settings["tempdir2"];
					if (tempdir2.IsString()) {
						this->tempDir2 = tempdir2.GetString();
					}

					json::Value& ksize = settings["ksize"];
					if (ksize.IsInt()) {
						this->ksize = ksize.GetInt();
					}
					else if (ksize.IsString()) {
						try {
							this->ksize = std::atoi(ksize.GetString());
						}
						catch (...) {
							this->ksize = AppSettings::defaultKSize;
						}
					}

					json::Value& buckets = settings["buckets"];
					if (buckets.IsInt()) {
						this->buckets = buckets.GetInt();
					}
					else if (buckets.IsString()) {
						try {
							this->buckets = std::atoi(buckets.GetString());
						}
						catch (...) {
							this->buckets = AppSettings::defaultBuckets;
						}
					}

					json::Value& threads = settings["threads"];
					if (threads.IsInt()) {
						this->threads = threads.GetInt();
					}
					else if (threads.IsString()) {
						try {
							this->threads = std::atoi(threads.GetString());
						}
						catch (...) {
							this->threads = std::thread::hardware_concurrency()/2;
							if (this->threads < 2) {
								this->threads = 2;
							}
						}
					}

					json::Value& stripes = settings["stripes"];
					if (stripes.IsInt()) {
						this->stripes = stripes.GetInt();
					}
					else if (stripes.IsString()) {
						try {
							this->stripes = std::atoi(stripes.GetString());
						}
						catch (...) {
							this->stripes = AppSettings::defaultStripes;
						}
					}

					json::Value& buffer = settings["buffer"];
					if (buffer.IsInt()) {
						this->buffer = buffer.GetInt();
					}
					else if (buffer.IsString()) {
						try {
							this->buffer = std::atoi(buffer.GetString());
						}
						catch (...) {
							this->buffer = AppSettings::defaultBuffer;
						}
					}

					json::Value& bitfield = settings["bitfield"];
					if (bitfield.IsInt()) {
						this->bitfield = bitfield.GetInt() == 0 ? false:true;
					}
					else if (buffer.IsString()) {
						if (buffer.GetString() == "yes" || buffer.GetString() == "true") {
							this->bitfield = true;
						}
						else {
							this->bitfield = false;
						}
					}
					else if (buffer.IsBool()) {
						this->bitfield = buffer.GetBool();
					}
				}
			}
			ifs.close();
		}
	}
}

void AppSettings::load()
{
	this->load(std::filesystem::current_path()/"settings.json");
}

void AppSettings::save(std::filesystem::path f)
{
	json::Document doc;
	doc.SetObject();
	json::Value settings(json::kObjectType);

	settings.AddMember("farmkey" ,json::StringRef(this->farmKey.c_str()), doc.GetAllocator());
	settings.AddMember("poolkey" ,json::StringRef(this->poolKey.c_str()), doc.GetAllocator());
	settings.AddMember("finaldir",json::StringRef(this->finalDir.c_str()), doc.GetAllocator());
	settings.AddMember("tempdir" ,json::StringRef(this->tempDir.c_str()), doc.GetAllocator());
	settings.AddMember("tempdir2",json::StringRef(this->tempDir2.c_str()), doc.GetAllocator());
	settings.AddMember("ksize"   ,this->ksize, doc.GetAllocator());
	settings.AddMember("buckets" ,this->buckets, doc.GetAllocator());
	settings.AddMember("stripes" ,this->stripes, doc.GetAllocator());
	settings.AddMember("threads" ,this->threads, doc.GetAllocator());
	settings.AddMember("buffer"  ,this->buffer, doc.GetAllocator());
	settings.AddMember("bitfield",this->bitfield, doc.GetAllocator());
	
	doc.AddMember("settings",settings, doc.GetAllocator());	
	std::ofstream ofs( f.string());
	if (ofs.is_open()) {
		json::OStreamWrapper osw { ofs };
		json::PrettyWriter<json::OStreamWrapper,json::Document::EncodingType,json::UTF8<>> writer { osw };
		doc.Accept( writer );
	}
}
