#include <algorithm>
#include <iostream>
#include <fstream>
#include <clocale>
#include <locale>
#include <codecvt>

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include "uint128_t/uint128_t.h"

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


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd) {
	LPWSTR *args;
	int nArgs;
	args = CommandLineToArgvW(GetCommandLineW(), &nArgs);
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
						std::cout << "  -n  --no-bitfield : use bitfield            (default set  )" << std::endl << std::endl;
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
							cli_create(farmkey,poolkey,dest,temp,temp2,filename,memo,id,ksize,buckets,stripes,nthreads,mem,!bitfield);
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
		
		return 0;
	}
	return 1;
}

constexpr auto ColorFromBytes = [](uint8_t r, uint8_t g, uint8_t b)
{
	return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f);
};

const ImVec4 bgColor           = ColorFromBytes(4, 16, 8);
const ImVec4 lightBgColor      = ColorFromBytes(40, 65, 50);
const ImVec4 veryLightBgColor  = ColorFromBytes(70, 115, 90);

const ImVec4 panelColor        = ColorFromBytes(80, 140, 110);
const ImVec4 panelHoverColor   = ColorFromBytes(30, 215, 140);
const ImVec4 panelActiveColor  = ColorFromBytes(0, 200, 120);

const ImVec4 textColor         = ColorFromBytes(200, 255, 210);
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
	colors[ImGuiCol_ScrollbarBg]          = borderColor;
	colors[ImGuiCol_ScrollbarGrab]        = lightBgColor;
	colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
	colors[ImGuiCol_ScrollbarGrabActive]  = panelActiveColor;
	colors[ImGuiCol_CheckMark]            = textColor;
	colors[ImGuiCol_SliderGrab]           = panelHoverColor;
	colors[ImGuiCol_SliderGrabActive]     = panelActiveColor;
	colors[ImGuiCol_Button]               = lightBgColor;
	colors[ImGuiCol_ButtonHovered]        = veryLightBgColor;
	colors[ImGuiCol_ButtonActive]         = panelHoverColor;
	colors[ImGuiCol_Header]               = panelColor;
	colors[ImGuiCol_HeaderHovered]        = panelHoverColor;
	colors[ImGuiCol_HeaderActive]         = panelActiveColor;
	colors[ImGuiCol_Separator]            = borderColor;
	colors[ImGuiCol_SeparatorHovered]     = borderColor;
	colors[ImGuiCol_SeparatorActive]      = borderColor;
	colors[ImGuiCol_ResizeGrip]           = bgColor;
	colors[ImGuiCol_ResizeGripHovered]    = lightBgColor;
	colors[ImGuiCol_ResizeGripActive]     = veryLightBgColor;
	colors[ImGuiCol_PlotLines]            = panelActiveColor;
	colors[ImGuiCol_PlotLinesHovered]     = panelHoverColor;
	colors[ImGuiCol_PlotHistogram]        = panelActiveColor;
	colors[ImGuiCol_PlotHistogramHovered] = panelHoverColor;
	colors[ImGuiCol_DragDropTarget]       = bgColor;
	colors[ImGuiCol_NavHighlight]         = bgColor;
	colors[ImGuiCol_Tab]                  = bgColor;
	colors[ImGuiCol_TabActive]            = panelActiveColor;
	colors[ImGuiCol_TabUnfocused]         = bgColor;
	colors[ImGuiCol_TabUnfocusedActive]   = panelActiveColor;
	colors[ImGuiCol_TabHovered]           = panelHoverColor;
	colors[ImGuiCol_TableHeaderBg]        = lightBgColor;

	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 4.0f;
	style.FrameRounding     = 7.0f;
	style.GrabRounding      = 4.0f;
	style.PopupRounding     = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.TabRounding       = 0.0f;
	style.FramePadding		= ImVec2(6.0f,3.0f);
	style.WindowPadding     = ImVec2(4.0f,0.0f);
	style.ItemSpacing		= ImVec2(2.0f,4.0f);
	style.AntiAliasedLines = true;
	style.AntiAliasedFill = true;
	style.TabRounding       = 0.0f;
}

const ImGuiTableFlags tableFlag = 
	ImGuiTableFlags_Resizable|
	ImGuiTableFlags_SizingFixedFit|
	ImGuiTableFlags_NoBordersInBody|
	ImGuiTableFlags_NoBordersInBodyUntilResize;

MainApp::MainApp(GLFWwindow* window) : ImFrame::ImApp(window) {
	glfwSetWindowSize(this->GetWindow(),800,400);
}

void MainApp::OnUpdate() {
	Style();
	glfwGetWindowPos(this->GetWindow(),&wx,&wy);
	glfwGetWindowSize(this->GetWindow(),&ww,&wh);
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
	}
	ImGui::End();
}

void MainApp::toolPage() {
	ImGui::PushID("toolpage");
		
	if(ImGui::BeginTable("toolTable",3,tableFlag)){
		ImGui::TableSetupScrollFreeze(1, 1);
		ImGui::TableSetupColumn("Create Job",ImGuiTableColumnFlags_WidthFixed,340.0f);
		ImGui::TableSetupColumn("Active Job",ImGuiTableColumnFlags_WidthFixed,230.0f);
		ImGui::TableSetupColumn("Job Status",ImGuiTableColumnFlags_WidthStretch,240);
		ImGui::TableHeadersRow();			

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		if (ImGui::BeginChild("col1", ImVec2(0.0f, 0.0f))) {
			if (ImGui::CollapsingHeader("Create Plot")) {
				ImGui::Indent(20.0f);
				this->createPlotDialog();
				ImGui::Unindent(20.0f);
			}
			if (ImGui::CollapsingHeader("Check Plot")) {
				ImGui::Text("Under development");
			}
			ImGui::EndChild();
		}

		ImGui::TableSetColumnIndex(1);
		if (JobManager::getInstance().countJob() < 1) {
			ImGui::Text("No Active Job, create from left panel");
		}
		else {
			if (ImGui::BeginChild("col2", ImVec2(0.0f, 0.0f))) {
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
				ImGui::EndChild();
			}
		}

		ImGui::TableSetColumnIndex(2);
		std::shared_ptr<Job> job = JobManager::getInstance().getSelectedJob();
		if (job) {
			if (ImGui::BeginChild(job->getTitle().c_str())) {
				job->drawStatusWidget();
				ImGui::EndChild();
			}
		}

		ImGui::EndTable();
	}
	ImGui::PopID();
}

void MainApp::statPage() {}

void MainApp::systemPage() {}

void MainApp::helpPage() {}

void MainApp::createPlotDialog() {
	JobCreatePlotParam* jobPram = JobCreatePlot::drawUI();

	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::Text("Job Name");
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-(jobPram?160.0f:90.0f));
	std::string jobName = "createplot-"+std::to_string(JobCreatePlot::jobIdCounter);
	ImGui::InputText("##jobName",&jobName);
	ImGui::PopItemWidth();
	if (jobPram) {
		ImGui::SameLine();
		ImGui::PushItemWidth(50.0f);
		if (ImGui::Button("Add Job")) {
			JobManager::getInstance().addJob(std::make_shared<JobCreatePlot>(jobName, *jobPram));
			JobCreatePlot::jobIdCounter++;
		}
		ImGui::PopItemWidth();
	}		
}
