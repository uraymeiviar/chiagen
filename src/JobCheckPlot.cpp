#include "JobCheckPlot.h"
#include "util.hpp"
#include "imgui.h"
#include "gui.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include "chiapos/verifier.hpp"
#include "bits.hpp"
#include <random>

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

	if (!uiDelPath.empty()) {
		this->paths.erase(
			std::remove_if(
				this->paths.begin(), 
				this->paths.end(),
				[&](auto f){
					bool del = f == uiDelPath;
					if (del) {
						uiDelPath = L"";
					}
					return del;
				}
			), 
			this->paths.end()
		);
	}

	if (!uiDelDirPath.empty()) {
		this->watchDirs.erase(
			std::remove_if(
				this->watchDirs.begin(), 
				this->watchDirs.end(),
				[&](auto d){
					bool del = d.first == uiDelDirPath;
					if (del) {
						uiDelDirPath = L"";
					}
					return del;
				}
			), 
			this->watchDirs.end()
		);
	}

	ImGui::Text("Iteration");
	ImGui::SameLine();
	if (ImGui::InputInt("##iteration", &this->iteration, 1, 10)) {
		if (this->iteration < 10) {
			this->iteration = 10;
		}
	}

	ImGui::Text("Randomize Challenge");
	ImGui::SameLine();
	ImGui::Checkbox("##Randomize", &this->randomizeChallenge);
	ImGui::ScopedSeparator();

	if (!this->paths.empty() || !this->watchDirs.empty()) {
		float height = 200;
		if (this->watchDirs.size() + this->paths.size() < 8) {
			height = (this->watchDirs.size() + this->paths.size())*25;
		}
		if (ImGui::BeginChild("CheckList", ImVec2(0, height))) {
			for (auto dir : this->watchDirs) {
				ImGui::PushID(dir.first.c_str());
				if (ImGui::Button("Del.")) {
					uiDelDirPath = dir.first;
				}
				ImGui::SameLine();
				bool selected = uiSelectedIsDir && (uiSelectedPath == dir.first);
				std::string label = ws2s(dir.first);
				if (dir.second) {
					label += " (recursive)";
				}
				if (ImGui::Selectable(label.c_str(), &selected)) {
					if (selected) {
						uiSelectedIsDir = true;
						uiSelectedPath = dir.first;
					}
				}
				ImGui::PopID();
			}
			for (auto f : this->paths) {
				ImGui::PushID(f.c_str());
				if (ImGui::Button("Del.")) {
					uiDelPath = f;
				}
				ImGui::SameLine();
				bool selected = uiSelectedIsDir && (uiSelectedPath == f);
				if (ImGui::Selectable(ws2s(f).c_str(), &selected)) {
					if (selected) {
						uiSelectedIsDir = false;
						uiSelectedPath = f;
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		if (ImGui::Button("Clear")) {
			this->paths.clear();
			this->watchDirs.clear();
			uiDelPath = L"";
			uiDelDirPath = L"";
		}
		ImGui::ScopedSeparator();
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
		ImGui::BeginGroupPanel();
		{
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::PushItemWidth(60.0f);
			ImGui::Text("Path");
			ImGui::PopItemWidth();
			ImGui::SameLine(60.0f);
			ImGui::PushItemWidth(fieldWidth-(80.0f + 105.0f));
			if (ImGui::InputText("##watchDir", &uiWatchDir)) {
				result |= true;
			}
			if (ImGui::IsItemHovered() && !uiWatchDir.empty()) {
				ImGui::BeginTooltip();
				tooltiipText(uiWatchDir.c_str());
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();
			
			ImGui::PushItemWidth(55.0f);
			ImGui::SameLine();
			if (ImGui::Button("Paste##watchDir")) {
				uiWatchDir = ImGui::GetClipboardText();
				result |= true;
			}
			ImGui::PopItemWidth();

			ImGui::SameLine();

			ImGui::PushItemWidth(55.0f);	
			if (ImGui::Button("Select##watchDir")) {
				std::optional<std::filesystem::path> dirPath = ImFrame::PickFolderDialog();
				if (dirPath) {
					uiWatchDir = dirPath->string();
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		ImGui::EndGroupPanel();

		result |= ImGui::Checkbox("Recursive",&uiRecursive);
		ImGui::SameLine();
		if (ImGui::Button("Add To CheckList##watchDir")) {
			auto found = std::find_if(
				this->watchDirs.begin(), 
				this->watchDirs.end(), 
				[=](std::pair<std::wstring, bool>& item) {
					return ws2s(item.first) == uiWatchDir;
				}
			);
			if (found == this->watchDirs.end()) {
				std::filesystem::path watchDirPath(uiWatchDir);
				if (std::filesystem::exists(watchDirPath)) {
					if (!std::filesystem::is_directory(watchDirPath)) {
						watchDirPath = watchDirPath.parent_path();
					}
					this->watchDirs.push_back(std::make_pair(watchDirPath.wstring(), uiRecursive));
				}
			}
			uiWatchDir = "";
			result |= true;
		}	
	}
	else {
		static std::string watchFile;
		static std::vector<std::wstring> selectedFiles;
		static std::wstring delSelectedFile;

		if (!delSelectedFile.empty()) {
			selectedFiles.erase(
				std::remove_if(
					selectedFiles.begin(), 
					selectedFiles.end(),
					[](auto f){
						bool del = f == delSelectedFile;
						if (del) {
							delSelectedFile = L"";
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
						ImGui::Text(ws2s(sf).c_str());
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
							selectedFiles.push_back(p.wstring());
						}
					}
					result |= true;
				}
			ImGui::PopItemWidth();
		}
		ImGui::EndGroupPanel();

		if (ImGui::Button("Add To CheckList##watchFile")) {
			for (auto f : selectedFiles) {
				if (std::filesystem::exists(f) && std::filesystem::is_regular_file(f)) {
					if (std::find(this->paths.begin(), this->paths.end(), f) == this->paths.end()) {
						this->paths.push_back(f);
					}
				}	
			}
			selectedFiles.clear();
			if (!watchFile.empty()) {
				if (std::filesystem::exists(watchFile) && std::filesystem::is_regular_file(watchFile)) {
					if (std::find(this->paths.begin(), this->paths.end(), s2ws(watchFile)) == this->paths.end()) {
						this->paths.push_back(s2ws(watchFile));
					}
				}
			}
			watchFile = "";
			result |= true;
		}
	}

	ImGui::ScopedSeparator();
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
	: Job(title, originalTitle), startRule(startRuleParam), finishRule(finishRuleParam), param(param)
{
	this->startEvent = std::make_shared<JobEvent>("check-start", this->getOriginalTitle());
	this->finishEvent = std::make_shared<JobEvent>("check-finish", this->getOriginalTitle());
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
	if (!this->results.empty()) {
		ImGui::ScopedSeparator();
		size_t successCount = 0;
		size_t failedCount = 0;
		size_t totalCount = 0;
		size_t totalPlots = 0;
		float sumQuality = 0.0f;
		float avgQuality = 0.0f;
		float maxQuality = 0.0f;
		float minQuality = 2.5f;
		for (const auto& r : this->results) {
			if (r->iterProgress > 0) {
				successCount += r->success.size();
				failedCount += r->fails.size();
				totalCount += r->success.size() + r->fails.size();				
				float plotQuality = (float)r->success.size() / (float)(r->iter);
				sumQuality += plotQuality;
				if (plotQuality > maxQuality) {
					maxQuality = plotQuality;
				}
				if (plotQuality < minQuality) {
					minQuality = plotQuality;
				}
			}
			totalPlots++;
		}
		avgQuality = sumQuality / (float)totalPlots;
		ImGui::Text("Total Plots %d", totalPlots);
		ImGui::ProgressBar(avgQuality);
		ImGui::Text("min %.1f %%", minQuality * 100.0f);
		ImGui::SameLine();
		ImGui::Text("avg %.1f %%", avgQuality * 100.0f);
		ImGui::SameLine();
		ImGui::Text("max %.1f %%", maxQuality * 100.0f);
		if (ImGui::CollapsingHeader("Detailed Results")) {
			for (const auto& r : this->results) {
				ImGui::PushID(r.get());
				float plotQuality = (float)r->success.size() / (float)(r->iter);
				ImGui::Text(r->id.c_str());
				ImGui::Indent(20.0f);
				ImGui::Text("path %s", ws2s(r->filePath).c_str());
				ImGui::Text("kSize %d", r->kSize);
				ImGui::Text("iteration done %d / %d ,quality %.1f", r->iterProgress, r->iter, plotQuality*100.0f);
				ImGui::ProgressBar(plotQuality);
				if (!r->fails.empty() && ImGui::CollapsingHeader((std::string("Failed ") + std::to_string(r->fails.size())).c_str())) {
					ImGui::Indent(20.0f);
					if (ImGui::BeginChild((std::string("##failed") + r->id).c_str(), ImVec2(0, 180))){
						for (const auto& iter : r->fails) {
							ImGui::PushID(iter.get());
							ImGui::BeginGroupPanel();
							ImGui::TextWrapped("challenge %s", iter->challenge.c_str());
							ImGui::EndGroupPanel();
							ImGui::PopID();
						}
					}
					ImGui::EndChild();
					ImGui::Unindent();
				}
				if (!r->success.empty() && ImGui::CollapsingHeader((std::string("Success ") + std::to_string(r->success.size())).c_str())) {
					ImGui::Indent(20.0f);
					if (ImGui::BeginChild((std::string("##sucess") + r->id).c_str(), ImVec2(0, 180))){
						for (const auto& iter : r->success) {
							ImGui::PushID(iter.get());
							ImGui::BeginGroupPanel();
							ImGui::TextWrapped("challenge %s", iter->challenge.c_str());
							ImGui::TextWrapped("proof %s", iter->proof.c_str());
							ImGui::EndGroupPanel();
							ImGui::PopID();
						}
					}
					ImGui::EndChild();
					ImGui::Unindent();
				}
				ImGui::Unindent();
				ImGui::PopID();
			}
		}
	}

	ImGui::ScopedSeparator();
	if (!this->isRunning()) {
		if (ImGui::CollapsingHeader("Plot Parameters")) {
			result &= this->drawEditor();
		}
	}

	if (this->activity && this->isRunning()) {
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
	if (this->activity) {
		this->results.clear();
		this->startEvent->trigger(this->shared_from_this());
		std::vector<std::wstring> files;
		for (auto f : this->param.paths) {
			files.push_back(f);
		}
		for (auto d : this->param.watchDirs) {
			if (d.second) {
				for (const auto& f : std::filesystem::recursive_directory_iterator(std::filesystem::path(d.first))) {
					if (lowercase(f.path().extension().string()) == ".plot") {
						files.push_back(f.path().wstring());
					}
				}
			}
			else {
				for (const auto& f : std::filesystem::directory_iterator(std::filesystem::path(d.first))) {
					if (lowercase(f.path().extension().string()) == ".plot") {
						files.push_back(f.path().wstring());
					}
				}
			}
		}
		this->activity->mainRoutine = [=](JobActivityState*) {
			std::random_device rd;
			std::mt19937 mt(rd());
			std::uniform_int_distribution<int> dist(0, 65535);

			Verifier verifier;
			this->activity->totalWorkItem = files.size()*this->param.iteration;
			size_t startIterNum = 0;
			if (this->param.randomizeChallenge) {
				startIterNum = dist(mt);
			}

			for (auto f : files) {
				this->results.push_back(std::make_shared<JobCheckPlotResult>(f,this->param.iteration));
			}
			for (auto f : this->results) {
				f->iterProgress = 0;
				for (size_t i = 0; i < f->iter; i++) {
					size_t num = startIterNum + i;
					f->iterProgress++;
					std::shared_ptr<JobCheckPlotIterationResult> iterResult = std::make_shared<JobCheckPlotIterationResult>();

					std::vector<unsigned char> hash_input = intToBytes(num, 4);
					hash_input.insert(hash_input.end(), &f->id_bytes[0], &f->id_bytes[32]);

					std::vector<unsigned char> hash(picosha2::k_digest_size);
					picosha2::hash256(hash_input.begin(), hash_input.end(), hash.begin(), hash.end());

					iterResult->challenge = HexStr(hash.data(), 256 / 8);

					try {
						std::vector<LargeBits> qualities = f->prover.GetQualitiesForChallenge(hash.data());

						for (size_t i = 0; i < qualities.size(); i++) {
							LargeBits proof = f->prover.GetFullProof(hash.data(), i);
							uint8_t *proof_data = new uint8_t[proof.GetSize() / 8];
							proof.ToBytes(proof_data);
							JobManager::getInstance().log("i: " + std::to_string(num),this->shared_from_this());
							JobManager::getInstance().log("challenge: 0x" + HexStr(hash.data(), 256 / 8),this->shared_from_this());	

							LargeBits quality =
								verifier.ValidateProof(f->id_bytes, f->kSize, hash.data(), proof_data,f->kSize * 8);
							if (quality.GetSize() == 256 && quality == qualities[i]) {
								JobManager::getInstance().log("proof: 0x" + HexStr(proof_data, f->kSize * 8),this->shared_from_this());
								JobManager::getInstance().log("quality: " + quality.ToString(),this->shared_from_this());
								JobManager::getInstance().log("Proof verification suceeded. k = " + std::to_string(static_cast<int>(f->kSize)),this->shared_from_this());
								iterResult->proof = HexStr(proof_data, f->kSize * 8);
								f->success.push_back(iterResult);
							} else {
								JobManager::getInstance().logErr("Proof verification failed.",this->shared_from_this());
								f->fails.push_back(iterResult);
							}
							delete[] proof_data;
						}
					} catch (const std::exception& error) {
						JobManager::getInstance().logErr("Proof verification failed." + std::string(error.what()),this->shared_from_this());
						f->fails.push_back(iterResult);
					}
					this->activity->completedWorkItem++;
				}
				startIterNum += f->iter;
			}
		};
		this->finishEvent->trigger(this->shared_from_this());
	}
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

	ImGui::ScopedSeparator();
	
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
