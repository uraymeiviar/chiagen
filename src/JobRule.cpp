#include "JobRule.h"
#include "imgui.h"
#include "ImFrame.h"
#include "Imgui/misc/cpp/imgui_stdlib.h"
#include "gui.hpp"

bool JobStartRuleParam::drawEditor()
{
	bool result = false;
	if (ImGui::Checkbox("Paused", &this->startPaused)) {
		if (this->startPaused) {
			this->startDelayed = false;
			this->startConditional = false;
			this->startImmediate = false;
			this->startOnEvent = false;
		}
		result |= true;
	}
	if (ImGui::Checkbox("Immediately", &this->startImmediate)) {
		if (this->startImmediate) {
			this->startDelayed = false;
			this->startConditional = false;
			this->startPaused = false;
			this->startOnEvent = false;
		}
		result |= true;
	}

	if (ImGui::Checkbox("Delayed",&this->startDelayed)) {
		if (this->startDelayed) {
			this->startImmediate = false;
			this->startPaused = false;
			this->startOnEvent = false;
		}
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
		if (this->startOnEvent) {
			this->startImmediate = false;
			this->startPaused = false;
		}
		result |= true;
	}
	if (this->startOnEvent) {
		ImGui::Indent(20.0f);
		std::vector<std::string> sources = JobManager::getInstance().getAvailableEventEmitters();
		if (sources.empty()) {
			ImGui::Text("No event to attach to, create one or more active jobs first");
			this->eventToRespond.name = "";
			this->eventToRespond.type = "";
		}
		else {
			static bool anyJob = false;
			ImGui::Checkbox("From Any Jobs", &anyJob);
			if (!anyJob) {				
				if (this->eventToRespond.name.empty()) {
					this->eventToRespond.name = sources.at(0);
				}
				ImGui::Text("Select Job:");
				if (ImGui::BeginCombo("##SelectJob",this->eventToRespond.name.c_str())) {
					for (auto name : sources) {
						bool selected = name == this->eventToRespond.name;
						if (ImGui::Selectable(name.c_str(), &selected)) {
							this->eventToRespond.name = name;
						}
						if (selected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
				std::shared_ptr<Job> selectedJob = JobManager::getInstance().getActiveJobByName(this->eventToRespond.name, true);
				if (selectedJob != nullptr) {
					if (this->eventToRespond.type.empty()) {
						this->eventToRespond.type = selectedJob->events.at(0)->getType();
					}
					ImGui::Text("Event Type:");
					if (ImGui::BeginCombo("##EventType",this->eventToRespond.type.c_str())) {
						for (auto jobEvent : selectedJob->events) {
							bool selected = this->eventToRespond.type == jobEvent->getType();
							if (ImGui::Selectable(jobEvent->getType().c_str(), &selected)) {
								this->eventToRespond.type = jobEvent->getType();
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
				ImGui::Text("Event Type:");
				static std::string selectedName = this->eventToRespond.type;

				std::vector<std::string> eventNames = JobManager::getInstance().getAvailableEventTypes();
				if (!eventNames.empty()) {
					if (selectedName.empty()) {
						selectedName = eventNames.at(0);
					}
					else {
						if (std::find(eventNames.begin(), eventNames.end(), selectedName) == eventNames.end()) {
							selectedName = eventNames.at(0);
						}
					}

					if (ImGui::BeginCombo("##EventType",selectedName.c_str())) {
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
				else {
					ImGui::Text("No event to attach to, create one or more active jobs first");
					this->eventToRespond.name = "";
					this->eventToRespond.type = "";
				}
			}
		}
		ImGui::Unindent(20.0f);
	}
	else {
		this->eventToRespond.name = "";
		this->eventToRespond.type = "";
	}

	if (ImGui::Checkbox("Conditional",&this->startConditional)) {
		if (this->startConditional) {
			this->startImmediate = false;
			this->startPaused = false;
		}
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
	if (!this->startOnEvent && 
		!this->startDelayed && 
		!this->startConditional && 
		!this->startPaused) {
		this->startImmediate = true;
	}
	return result;
}

JobStartRule::JobStartRule() : JobRule()
{
	this->creationTime = std::chrono::system_clock::now();
}

JobStartRule::JobStartRule(const JobStartRuleParam& param)
	: param(param)
{
	this->creationTime = std::chrono::system_clock::now();
}

bool JobStartRule::drawEditor()
{
	return this->param.drawEditor();
}

bool JobStartRule::drawItemWidget()
{
	if (this->param.startDelayed) {
		std::chrono::minutes delayDuration(this->param.startDelayedMinute);
		std::chrono::time_point<std::chrono::system_clock> startTime = this->creationTime + delayDuration;
		std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
		std::chrono::duration<float> delta = startTime - currentTime;
		ImGui::Text("will start in %.1f hour ", delta.count()/3600.0f);
	}
	if (this->param.startPaused) {
		ImGui::Text("Job will be started manually (paused)");
	}
	if (this->param.startConditional) {
		if (this->param.startCondActiveJob) {
			ImGui::Text("start if active job < %d", this->param.startCondActiveJobCount);
		}
		if (this->param.startCondTime) {
			ImGui::Text("start within %02d:00 - %2d:00", this->param.startCondTimeStart, this->param.startCondTimeEnd);
		}
	}
	if (this->param.startOnEvent) {
		if (!this->param.eventToRespond.name.empty()) {
			ImGui::Text("Waiting for event %s from %s", this->param.eventToRespond.type.c_str(), this->param.eventToRespond.name.c_str());
		}
		else {
			ImGui::Text("Waiting for event %s", this->param.eventToRespond.type.c_str());
		}
	}
	return true;
}

bool JobStartRule::evaluate() {
	if (!this->param.eventToRespond.isEmpty()) {
		return false;
	}
	else {
		return this->isRuleFullfilled();
	}
}

void JobStartRule::handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source)
{
	if (!this->param.eventToRespond.isEmpty()) {
		if (this->param.eventToRespond.match(jobEvent.get())) {
			if (this->isRuleFullfilled()) {
				if (this->onStartTrigger) {
					this->onStartTrigger();
				}
			}
		}
	}
}

JobStartRuleParam& JobStartRule::getParam()
{
	return this->param;
}

JobStartRuleParam& JobStartRule::getRelaunchParam()
{
	return this->param;
}

bool JobStartRule::isRuleFullfilled()
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
			if ((this->param.startCondTimeStart < t->tm_hour) && 
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

bool JobFinishRuleParam::drawEditor()
{
	bool result = false;

	result |= ImGui::Checkbox("Relaunch Job",&this->repeatJob);
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
				filters[0].name = "program";
				filters[0].spec = "exe";
				filters[1].name = "batch script";
				filters[1].spec = "bat";
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

JobFinishRule::JobFinishRule() : JobRule()
{

}

JobFinishRule::JobFinishRule(const JobFinishRuleParam& param)
	: param(param)
{

}

bool JobFinishRule::drawEditor()
{
	return this->param.drawEditor();
}

bool JobFinishRule::drawItemWidget() {
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

bool JobFinishRule::relaunchAfterFinish()
{
	if (this->param.repeatIndefinite || this->param.repeatCount > 1) {
		return true;
	}
	else {
		return false;
	}
}

JobFinishRuleParam& JobFinishRule::getParam()
{
	return this->param;
}

JobFinishRuleParam& JobFinishRule::getRelaunchParam()
{
	if (!this->param.repeatIndefinite) {
		this->param.repeatCount--;
		if (this->param.repeatCount < 0) {
			this->param.repeatCount = 0;
		}
	}
	return this->param;
}