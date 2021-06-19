#include "JobCheckPlot.h"
#include "util.hpp"
#include "imgui.h"
#include "gui.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"

FactoryRegistration<JobCheckPlotFactory> JobCheckPlotFactoryRegistration;
int JobCheckPlot::jobIdCounter = 1;

JobCheckPlotParam::JobCheckPlotParam(const JobCheckPlotParam& rhs)
{
	this->paths = rhs.paths;
	this->watchDirs = rhs.watchDirs;
	this->iteration = rhs.iteration;
}

JobCheckPlotParam::JobCheckPlotParam()
{
	pickPlotFileFilter.push_back(ImFrame::Filter());
	pickPlotFileFilter[0].name = "chia plot";
	pickPlotFileFilter[0].spec = "plot";
}

bool JobCheckPlotParam::drawEditor()
{
	bool result = false;
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	static bool selectedIsDir = true;
	static std::filesystem::path selectedPath;
	static std::string delPath;
	static std::string delDirPath;

	if (!delPath.empty()) {
		this->paths.erase(
			std::remove_if(
				this->paths.begin(), 
				this->paths.end(),
				[](auto f){
					bool del = f == delPath;
					if (del) {
						delPath = "";
					}
					return del;
				}
			), 
			this->paths.end()
		);
	}

	if (!delDirPath.empty()) {
		this->watchDirs.erase(
			std::remove_if(
				this->watchDirs.begin(), 
				this->watchDirs.end(),
				[](auto d){
					bool del = d.first == delDirPath;
					if (del) {
						delDirPath = "";
					}
					return del;
				}
			), 
			this->watchDirs.end()
		);
	}

	if (!this->paths.empty() || !this->watchDirs.empty()) {
		if (ImGui::BeginChild("CheckList", ImVec2(0, 180))) {
			for (auto dir : this->watchDirs) {
				ImGui::PushID(dir.first.c_str());
				if (ImGui::Button("Del.")) {
					delDirPath = dir.first;
				}
				ImGui::SameLine();
				bool selected = selectedIsDir && (selectedPath == dir.first);
				std::string label = dir.first;
				if (dir.second) {
					label += " (recursive)";
				}
				if (ImGui::Selectable(label.c_str(), &selected)) {
					if (selected) {
						selectedIsDir = true;
						selectedPath = dir.first;
					}
				}
				ImGui::PopID();
			}
			for (auto f : this->paths) {
				ImGui::PushID(f.c_str());
				if (ImGui::Button("Del.")) {
					delPath = f;
				}
				ImGui::SameLine();
				bool selected = selectedIsDir && (selectedPath == f);
				if (ImGui::Selectable(f.c_str(), &selected)) {
					if (selected) {
						selectedIsDir = false;
						selectedPath = f;
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		if (ImGui::Button("Clear")) {
			this->paths.clear();
			this->watchDirs.clear();
			delPath = "";
			delDirPath = "";
		}
		ImGui::Separator();
	}
	
	int static activeTab = 0;
	if (ImGui::BeginTabBar("##PlotEditor")) {
		if(ImGui::BeginTabItem("Add Folder")){
			activeTab = 0;
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Add File")){
			activeTab = 1;
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if (activeTab == 0) {
		static std::string watchDir;
		static bool recursive = false;
		ImGui::BeginGroupPanel();
		{
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::PushItemWidth(60.0f);
			ImGui::Text("Path");
			ImGui::PopItemWidth();
			ImGui::SameLine(60.0f);
			ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
			if (ImGui::InputText("##watchDir", &watchDir)) {
				result |= true;
			}
			if (ImGui::IsItemHovered() && !watchDir.empty()) {
				ImGui::BeginTooltip();
				tooltiipText(watchDir.c_str());
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();
			
			ImGui::PushItemWidth(55.0f);
			ImGui::SameLine();
			if (ImGui::Button("Paste##watchDir")) {
				watchDir = ImGui::GetClipboardText();
				result |= true;
			}
			ImGui::PopItemWidth();

			ImGui::SameLine();

			ImGui::PushItemWidth(55.0f);	
			if (ImGui::Button("Select##watchDir")) {
				std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
				if (dirPath) {
					watchDir = dirPath->string();
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		ImGui::EndGroupPanel();

		result |= ImGui::Checkbox("Recursive",&recursive);
		ImGui::SameLine();
		if (ImGui::Button("Add To CheckList##watchDir")) {
			auto found = std::find_if(
				this->watchDirs.begin(), 
				this->watchDirs.end(), 
				[=](std::pair<std::string, bool>& item) {
					return item.first == watchDir;
				}
			);
			if (found == this->watchDirs.end()) {
				this->watchDirs.push_back(std::make_pair(watchDir, recursive));
			}
			watchDir = "";
			result |= true;
		}	
	}
	else {
		static std::string watchFile;
		static std::vector<std::string> selectedFiles;
		static std::string delSelectedFile;

		if (!delSelectedFile.empty()) {
			selectedFiles.erase(
				std::remove_if(
					selectedFiles.begin(), 
					selectedFiles.end(),
					[](auto f){
						bool del = f == delSelectedFile;
						if (del) {
							delSelectedFile = "";
						}
						return del;
					}
				), 
				selectedFiles.end()
			);
		}

		ImGui::BeginGroupPanel();
		{
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
									
			for (auto sf : selectedFiles) {
				ImGui::PushID(sf.c_str());
					ImGui::PushItemWidth(40.0f);
						if (ImGui::Button("Del.")) {
							delSelectedFile = sf;
						}
					ImGui::PopItemWidth();

					ImGui::SameLine(40.0f);
					ImGui::PushItemWidth(fieldWidth-60.0f);
						if (ImGui::InputText("##editWatchFile", &sf)) {
							result |= true;
						}
					ImGui::PopItemWidth();
				ImGui::PopID();
			}
			
			ImGui::PushItemWidth(40.0f);
				ImGui::Text("Path");
			ImGui::PopItemWidth();

			ImGui::SameLine(40.0f);
			ImGui::PushItemWidth(fieldWidth-(40.0f + 120.0f));
				if (ImGui::InputText("##watchFile", &watchFile)) {
					result |= true;
				}
				if (ImGui::IsItemHovered() && !watchFile.empty()) {
					ImGui::BeginTooltip();
						tooltiipText(watchFile.c_str());
					ImGui::EndTooltip();
				}
			ImGui::PopItemWidth();

			ImGui::SameLine();
			ImGui::PushItemWidth(55.0f);
				if (ImGui::Button("Paste##watchFile")) {
					watchFile = ImGui::GetClipboardText();
					result |= true;
				}
			ImGui::PopItemWidth();

			ImGui::SameLine();
				ImGui::PushItemWidth(55.0f);			
				if (ImGui::Button("Select##watchFile")) {
					std::optional<std::vector<std::filesystem::path>> fPaths = ImFrame::OpenFilesDialog(this->pickPlotFileFilter);
					if (fPaths) {
						selectedFiles.clear();
						for (auto p : fPaths.value()) {
							selectedFiles.push_back(p.string());
						}
					}
					result |= true;
				}
			ImGui::PopItemWidth();
		}
		ImGui::EndGroupPanel();

		if (ImGui::Button("Add To CheckList##watchFile")) {
			for (auto f : selectedFiles) {
				if (std::find(this->paths.begin(), this->paths.end(), f) == this->paths.end()) {
					this->paths.push_back(f);
				}		
			}
			selectedFiles.clear();
			if (!watchFile.empty()) {
				if (std::find(this->paths.begin(), this->paths.end(), watchFile) == this->paths.end()) {
					this->paths.push_back(watchFile);
				}
			}
			watchFile = "";
			result |= true;
		}
	}

	ImGui::Separator();
	return !this->paths.empty() || !this->watchDirs.empty();
}

JobCheckPlot::JobCheckPlot(std::string title, std::string originalTitle)
	: Job(title, originalTitle)
{

}

JobCheckPlot::JobCheckPlot(std::string title, std::string originalTitle, JobCheckPlotParam& param)
	: Job(title, originalTitle), param(param)
{

}

JobCheckPlot::JobCheckPlot(std::string title, std::string originalTitle, JobCheckPlotParam& param, 
	JobStartRuleParam& startRuleParam, JobFinishRuleParam& finishRuleParam)
	: Job(title, originalTitle), startRule(startRuleParam), finishRule(finishRuleParam)
{

}

JobRule* JobCheckPlot::getStartRule()
{
	return &this->startRule;
}

JobRule* JobCheckPlot::getFinishRule()
{
	return &this->finishRule;
}

bool JobCheckPlot::drawEditor()
{
	bool result = this->param.drawEditor();
	if (ImGui::CollapsingHeader("Start Rule")) {
		ImGui::Indent(20.0f);
		result |= this->startRule.drawEditor();
		ImGui::Unindent(20.0f);
	}	

	if (ImGui::CollapsingHeader("Finish Rule")) {
		ImGui::Indent(20.0f);
		result |= this->finishRule.drawEditor();
		ImGui::Unindent(20.0f);
	}
	return result;
}

bool JobCheckPlot::drawItemWidget()
{
	bool result = Job::drawItemWidget();		

	if (!this->isRunning()) {			
		if (this->startRule.drawItemWidget()) {
			ImGui::ScopedSeparator();
		}
	}
	result &= this->finishRule.drawItemWidget();
	return result;
}

bool JobCheckPlot::drawStatusWidget()
{
	bool result = Job::drawStatusWidget();
	ImGui::ScopedSeparator();
	if (!this->isRunning()) {
		result &= this->drawEditor();
	}
	else if (this->activity) {
		this->activity->drawStatusWidget();
		if (ImGui::CollapsingHeader("Plot Parameters")) {
			ImGui::TextWrapped("changing these values, won\'t affect running process, if the job is relaunched, it will use these new parameters");
			ImGui::Indent(20.0f);
			result &= this->drawEditor();
			ImGui::Unindent(20.0f);
		}		
	}
	return result;
}

bool JobCheckPlot::relaunchAfterFinish()
{
	return this->finishRule.relaunchAfterFinish();
}

std::shared_ptr<Job> JobCheckPlot::relaunch()
{
	JobStartRule* startRule = dynamic_cast<JobStartRule*>(this->getStartRule());
	JobFinishRule* finishRule = dynamic_cast<JobFinishRule*>(this->getFinishRule());
	JobStartRuleParam& startParam = startRule->getRelaunchParam();
	JobFinishRuleParam& finishParam = finishRule->getRelaunchParam();
	if (!finishParam.repeatIndefinite && finishParam.repeatCount <= 0) {
		startParam.startPaused = true;
	}
	auto newJob = std::make_shared<JobCheckPlot>(
		this->getOriginalTitle()+"#"+systemClockToStr(std::chrono::system_clock::now()),
		this->getOriginalTitle(),
		this->param, 
		startParam,
		finishParam
	);
	return newJob;
}

void JobCheckPlot::initActivity()
{
	Job::initActivity();
}

std::string JobCheckPlotFactory::getName()
{
	return "Check Plot Files";
}

std::shared_ptr<Job> JobCheckPlotFactory::create(std::string jobName)
{
	return std::make_shared<JobCheckPlot>(
		jobName, jobName, this->param, this->startRuleParam, this->finishRuleParam
	);
}

bool JobCheckPlotFactory::drawEditor()
{
	bool result = this->param.drawEditor();
	if (ImGui::CollapsingHeader("Start Rule")) {
		ImGui::Indent(20.0f);
		result |= this->startRuleParam.drawEditor();
		ImGui::Unindent(20.0f);
	}

	if (ImGui::CollapsingHeader("Finish Rule")) {
		ImGui::Indent(20.0f);
		result |= this->finishRuleParam.drawEditor();
		ImGui::Unindent(20.0f);
	}

	ImGui::Separator();
	
	float fieldWidth = ImGui::GetWindowContentRegionWidth();

	ImGui::Text("Job Name");
	ImGui::SameLine(90.0f);
	ImGui::PushItemWidth(fieldWidth-160.0f);
	std::string jobName = "checkplot-"+std::to_string(JobCheckPlot::jobIdCounter);
	ImGui::InputText("##jobName",&jobName);
	ImGui::PopItemWidth();

	ImGui::SameLine();
	ImGui::PushItemWidth(50.0f);
	if(!result){
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}
	if (ImGui::Button("Add Job")) {
		if (result) {
			JobManager::getInstance().addJob(this->create(jobName));
			JobCheckPlot::jobIdCounter++;
		}
	}
	if (!result) {
		ImGui::PopStyleVar();
	}
	ImGui::PopItemWidth();


	return result;
}
