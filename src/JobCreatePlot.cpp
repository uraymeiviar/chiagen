#include "JobCreatePlot.h"
#include "ImFrame.h"
#include "imgui.h"
#include "gui.hpp"
#include "Imgui/misc/cpp/imgui_stdlib.h"

JobCreatePlot::JobCreatePlot(std::string title, 
	const JobCratePlotStartRuleParam& startParam, 
	const JobCreatePlotFinishRuleParam& finishParam)
	: Job(title),
	  startRule(startParam),
	  finishRule(finishParam)
{
	this->startRule.onStartTrigger = [=]() {
		this->start(true);
	};
	this->init();
}

JobCreatePlot::JobCreatePlot(std::string title) : Job(title)
{

}

int JobCreatePlot::jobIdCounter = 1;

JobRule* JobCreatePlot::getStartRule() {
	return &this->startRule;
}

JobRule* JobCreatePlot::getFinishRule() {
	return &this->finishRule;
}

bool JobCreatePlot::drawItemWidget() {
	bool result = Job::drawItemWidget();		

	if (!this->isRunning()) {			
		if (this->startRule.drawItemWidget()) {
			ImGui::ScopedSeparator();
		}
	}
	result &= this->finishRule.drawItemWidget();
	return result;
}

bool JobCreatePlot::drawStatusWidget() {
	bool result = Job::drawStatusWidget();
	ImGui::ScopedSeparator();
	if (!this->isRunning()) {
		result &= this->drawEditor();
	}
	else if (this->activity) {
		this->activity->drawStatusWidget();
	}
	return result;
}

bool JobCreatePlot::relaunchAfterFinish()
{
	return this->finishRule.relaunchAfterFinish();
}

void JobCreatePlot::init()
{
	this->startEvent = std::make_shared<JobEvent>("job-start");
	this->finishEvent = std::make_shared<JobEvent>("job-finish");
	this->phase1FinishEvent = std::make_shared<JobEvent>("phase1-finish");
	this->phase2FinishEvent = std::make_shared<JobEvent>("phase2-finish");
	this->phase3FinishEvent = std::make_shared<JobEvent>("phase3-finish");
	this->phase4FinishEvent = std::make_shared<JobEvent>("phase4-finish");
	this->events.push_back(this->startEvent);
	this->events.push_back(this->finishEvent);
	this->events.push_back(this->phase1FinishEvent);
	this->events.push_back(this->phase2FinishEvent);
	this->events.push_back(this->phase3FinishEvent);
	this->events.push_back(this->phase4FinishEvent);
}

bool JobCratePlotStartRuleParam::drawEditor()
{
	bool result = false;
	ImGui::Text("these start rule are under development");
	ImGui::Text("just use start immediately only for now");
	ImGui::Separator();
	if (ImGui::Checkbox("Paused", &this->startPaused)) {
		this->startDelayed = false;
		this->startConditional = false;
		this->startImmediate = false;
		this->startOnEvent = false;
		result |= true;
	}
	if (ImGui::Checkbox("Immediately", &this->startImmediate)) {
		this->startDelayed = false;
		this->startConditional = false;
		this->startPaused = false;
		this->startOnEvent = false;
		result |= true;
	}

	if (ImGui::Checkbox("Delayed",&this->startDelayed)) {
		this->startImmediate = false;
		this->startPaused = false;
		result |= true;
	}
	if (this->startDelayed) {
		ImGui::Indent(20.0f);
		float fieldWidth = ImGui::GetWindowContentRegionWidth();
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
		ImGui::Text("Delay (min)");
		ImGui::SameLine(80.0f);
		ImGui::PushItemWidth(fieldWidth-160.0f);
		if (ImGui::InputInt("##delay", &this->startDelayedMinute, 1, 5)) {
			if (this->startDelayedMinute < 1) {
				this->startDelayedMinute = 1;
			}
			result |= true;
		}
		ImGui::PopItemWidth();
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}

	if (ImGui::Checkbox("Wait for Event",&this->startOnEvent)) {
		this->startImmediate = false;
		this->startPaused = false;
		result |= true;
	}
	if (this->startOnEvent) {
		ImGui::Indent(20.0f);
		if (JobManager::getInstance().countJob() < 1) {
			ImGui::Text("No event to attach to, create one or more active jobs first");
			this->eventToRespond.name = "";
			this->eventToRespond.type = "";
		}
		else {
			static bool anyJob = false;
			ImGui::Checkbox("From Any Jobs", &anyJob);
			if (!anyJob) {
				static std::shared_ptr<Job> selectedJob = nullptr;
				static std::string preview = this->eventToRespond.name;
				if (preview.empty()) {
					if (selectedJob == nullptr) {
						preview = (*JobManager::getInstance().jobIteratorBegin())->getTitle();
					}
					else {
						preview = selectedJob->getTitle();
					}
				}
				ImGui::Text("Select Job:");
				if (ImGui::BeginCombo("##SelectJob",preview.c_str())) {
					for (auto it  = JobManager::getInstance().jobIteratorBegin(); 
							  it != JobManager::getInstance().jobIteratorEnd(); 
							  it++) {
						bool isSelected = selectedJob == *it;
						if (ImGui::Selectable((*it)->getTitle().c_str(), &isSelected)) {
							selectedJob = *it;
							this->eventToRespond.name = selectedJob->getTitle();
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
				if (selectedJob != nullptr) {
					static std::shared_ptr<JobEvent> selectedJobEvent = nullptr;
					static std::string jobEventPreview = "";
					if (selectedJobEvent == nullptr) {
						jobEventPreview = (*selectedJob->events.begin())->getType();
					}
					else {
						jobEventPreview = selectedJobEvent->getType();
					}
					ImGui::Text("Event Type:");
					if (ImGui::BeginCombo("##EventType",jobEventPreview.c_str())) {
						for (auto jobEvent : selectedJob->events) {
							bool selected = selectedJobEvent == jobEvent;
							if (ImGui::Selectable(jobEvent->getType().c_str(), &selected)) {
								selectedJobEvent = jobEvent;
								this->eventToRespond.type = selectedJobEvent->getType();
							}
							if (selected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
			}
			else {
				static std::string eventPreview = "";
				if (eventPreview.empty()) {
					eventPreview = (*(*JobManager::getInstance().jobIteratorBegin())->events.begin())->getType();
				}
				std::vector<std::string> eventNames;
				for (auto it  = JobManager::getInstance().jobIteratorBegin(); 
							it != JobManager::getInstance().jobIteratorEnd(); 
							it++) {
					for (auto jobEvent : (*it)->events) {
						if (std::find(eventNames.begin(), eventNames.end(), jobEvent->getType()) == eventNames.end()) {
							eventNames.push_back(jobEvent->getType());
						}
					}
				}
				ImGui::Text("Event Type:");
				static std::string selectedName = this->eventToRespond.name;				
				if (ImGui::BeginCombo("##EventType",eventPreview.c_str())) {
					for (auto name : eventNames) {
						bool selected = selectedName == name;
						if (ImGui::Selectable(name.c_str(), &selected)) {
							selectedName = name;
						}
						if (selected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
					this->eventToRespond.type = selectedName;
					this->eventToRespond.name = "";
				}
			}
		}
		ImGui::Unindent(20.0f);
	}

	if (ImGui::Checkbox("Conditional",&this->startConditional)) {
		this->startImmediate = false;
		this->startDelayed = false;
		this->startPaused = false;
		result |= true;
	}
	if (this->startConditional) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
			
		result |= ImGui::Checkbox("if Active Jobs",&this->startCondActiveJob);
		if (this->startCondActiveJob) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Less Than");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##lessthan", &this->startCondActiveJobCount, 1, 5)) {
				if (this->startCondActiveJobCount < 1) {
					this->startCondActiveJobCount = 1;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		result |= ImGui::Checkbox("If Time Within",&this->startCondTime);
		if (this->startCondTime) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Start (Hr)");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##start", &this->startCondTimeStart, 1, 5)) {
				if (this->startCondTimeStart < 0) {
					this->startCondTimeStart = 24;
				}
				if (this->startCondTimeStart > 24) {
					this->startCondTimeStart = 0;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
			ImGui::Text("End (Hr)");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##end", &this->startCondTimeEnd, 1, 5)) {
				if (this->startCondTimeEnd < 0) {
					this->startCondTimeEnd = 24;
				}
				if (this->startCondTimeEnd > 24) {
					this->startCondTimeEnd = 0;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
			
		ImGui::EndGroupPanel();
		ImGui::Unindent(20);
	}
	if (!this->startImmediate && !this->startDelayed && !this->startConditional && !this->startPaused) {
		this->startPaused = true;
	}
	return result;
}

JobCratePlotStartRule::JobCratePlotStartRule() : JobStartRule()
{
	this->creationTime = std::chrono::system_clock::now();
}

JobCratePlotStartRule::JobCratePlotStartRule(const JobCratePlotStartRuleParam& param)
	: param(param)
{
	this->creationTime = std::chrono::system_clock::now();
}

bool JobCratePlotStartRule::drawEditor()
{
	return this->param.drawEditor();
}

bool JobCratePlotStartRule::drawItemWidget()
{
	if (this->param.startDelayed) {
		std::chrono::minutes delayDuration(this->param.startDelayedMinute);
		std::chrono::time_point<std::chrono::system_clock> startTime = this->creationTime + delayDuration;
		std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
		std::chrono::duration<float> delta = startTime - currentTime;
		ImGui::Text("will start in %.1f hour ", delta.count()/3600.0f);
		return true;
	}
	else if (this->param.startPaused) {
		ImGui::Text("job paused");
		return true;
	}
	else if (this->param.startConditional) {
		bool result = false;
		if (this->param.startCondActiveJob) {
			ImGui::Text("start if active job < %d", this->param.startCondActiveJobCount);
			result |= true;
		}
		if (this->param.startCondTime) {
			ImGui::Text("start within %02d:00 - %2d:00", this->param.startCondTimeStart, this->param.startCondTimeEnd);
			result |= true;
		}
		return result;
	}
	return false;
}

bool JobCratePlotStartRule::evaluate() {
	if (!this->waitForEvent.isEmpty()) {
		return false;
	}
	else {
		return this->isRuleFullfilled();
	}
}

void JobCratePlotStartRule::handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source)
{
	if (!this->waitForEvent.isEmpty()) {
		if (this->waitForEvent.match(jobEvent.get())) {
			if (this->isRuleFullfilled()) {
				if (this->onStartTrigger) {
					this->onStartTrigger();
				}
			}
		}
	}
}

JobCratePlotStartRuleParam& JobCratePlotStartRule::getParam()
{
	return this->param;
}

JobCratePlotStartRuleParam& JobCratePlotStartRule::getRelaunchParam()
{
	return this->param;
}

bool JobCratePlotStartRule::isRuleFullfilled()
{
	bool result = true;
	if (this->param.startDelayed) {
		std::chrono::minutes delayDuration(this->param.startDelayedMinute);
		std::chrono::time_point<std::chrono::system_clock> startTime = this->creationTime + delayDuration;
		std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
		result = currentTime > startTime;
	}
	if (result && this->param.startConditional) {
		if (this->param.startCondTime) {
			std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
			std::time_t ct = std::chrono::system_clock::to_time_t(currentTime);
			std::tm* t = std::localtime(&ct);
			if ((this->param.startCondTimeStart > t->tm_hour) && 
				(t->tm_hour < this->param.startCondTimeEnd)) {
				result &= true;
			}
			else {
				result &= false;
			}
		}

		if (this->param.startCondActiveJob) {
			if (JobManager::getInstance().countRunningJob() < this->param.startCondActiveJobCount) {
				result &= true;
			}
			else {
				result &= false;
			}
		}
	}
	if (this->param.startPaused) {
		return false;
	}
	return result;
}

bool JobCreatePlotFinishRuleParam::drawEditor()
{
	bool result = false;
	ImGui::Text("these start rule are under development");
	ImGui::Text("just use start immediately only for now");
	ImGui::Separator();

	result |= ImGui::Checkbox("Relaunch",&this->repeatJob);
	if (this->repeatJob) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
		if (!this->repeatIndefinite) {
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Count");
			ImGui::SameLine(80.0f);
			ImGui::PushItemWidth(fieldWidth-150.0f);
			if (ImGui::InputInt("##repeatCount", &this->repeatCount, 1, 5)) {
				if (this->repeatCount < 1) {
					this->repeatCount = 1;
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		}
		result |= ImGui::Checkbox("Indefinite",&this->repeatIndefinite);
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}
	result |= ImGui::Checkbox("Launch Program",&this->execProg);
	if (this->execProg) {
		ImGui::Indent(20.0f);
		ImGui::BeginGroupPanel(ImVec2(-1.0f,0.0f));
			float fieldWidth = ImGui::GetWindowContentRegionWidth();
			ImGui::Text("Path");
			ImGui::SameLine(80.0f);
			static std::string execPath;
			ImGui::PushItemWidth(fieldWidth-240.0f);
			if (ImGui::InputText("##execPath", &execPath)) {
				this->progToExec = execPath;
				result |= true;
			}
			if (ImGui::IsItemHovered() && !execPath.empty()) {
				ImGui::BeginTooltip();
				tooltiipText(execPath.c_str());
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PushItemWidth(44.0f);
			if (ImGui::Button("Paste##execPath")) {
				execPath = ImGui::GetClipboardText();
				this->progToExec = execPath;
				result |= true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Select##execPath")) {
				std::vector<ImFrame::Filter> filters;
				filters.push_back(ImFrame::Filter());
				filters.push_back(ImFrame::Filter());
				filters[0].name = "exe";
				filters[0].spec = "*.exe";
				filters[1].name = "batch script";
				filters[1].spec = "*.bat";
				std::optional<std::filesystem::path> dirPath = ImFrame::OpenFileDialog(filters);
				if (dirPath) {
					execPath = dirPath->string();
					this->progToExec = dirPath.value();
				}
				result |= true;
			}
			ImGui::PopItemWidth();
		ImGui::EndGroupPanel();
		ImGui::Unindent(20.0f);
	}
	return result;
}

JobCreatePlotFinishRule::JobCreatePlotFinishRule() : JobFinishRule()
{

}

JobCreatePlotFinishRule::JobCreatePlotFinishRule(const JobCreatePlotFinishRuleParam& param)
	: param(param)
{

}

bool JobCreatePlotFinishRule::drawEditor()
{
	return this->param.drawEditor();
}

bool JobCreatePlotFinishRule::drawItemWidget() {
	if (this->param.repeatJob) {
		if (this->param.repeatIndefinite) {
			ImGui::Text("Job will repeat indefinitely");
			return true;
		}
		else {
			ImGui::Text("Job will repeat %d times", this->param.repeatCount);
			return true;
		}
	}
	return false;
}

bool JobCreatePlotFinishRule::relaunchAfterFinish()
{
	if (this->param.repeatIndefinite || this->param.repeatCount > 1) {
		return true;
	}
	else {
		return false;
	}
}

JobCreatePlotFinishRuleParam& JobCreatePlotFinishRule::getParam()
{
	return this->param;
}

JobCreatePlotFinishRuleParam& JobCreatePlotFinishRule::getRelaunchParam()
{
	if (!this->param.repeatIndefinite) {
		this->param.repeatCount--;
		if (this->param.repeatCount < 1) {
			this->param.repeatCount = 1;
		}
	}
	return this->param;
}

void CreatePlotContext::log(std::string text)
{
	JobManager::getInstance().log(text, this->job);
}

void CreatePlotContext::logErr(std::string text)
{
	JobManager::getInstance().logErr(text, this->job);
}

std::shared_ptr<JobTaskItem> CreatePlotContext::getCurrentTask()
{
	if (this->tasks.empty()) {
		return this->job->activity;
	}
	else {
		return this->tasks.top();
	}
}

std::shared_ptr<JobTaskItem> CreatePlotContext::pushTask(std::string name, std::shared_ptr<JobTaskItem> parent)
{
	std::shared_ptr<JobTaskItem> newTask = std::make_shared<JobTaskItem>(name);
	parent->addChild(newTask);
	this->tasks.push(newTask);
	return newTask;
}

std::shared_ptr<JobTaskItem> CreatePlotContext::popTask(bool finish)
{
	std::shared_ptr<JobTaskItem> result = this->tasks.top();
	if (finish) {
		result->stop(finish);
	}	
	this->tasks.pop();
	return result;
}


