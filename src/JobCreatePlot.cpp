#include "JobCreatePlot.h"
#include "ImFrame.h"
#include "imgui.h"

JobCreatePlot::JobCreatePlot(std::string title, 
	std::string originalTitle,
	const JobStartRuleParam& startParam, 
	const JobFinishRuleParam& finishParam)
	: Job(title, originalTitle),
	  startRule(startParam),
	  finishRule(finishParam)
{
	this->startRule.onStartTrigger = [=]() {
		this->start(true);
	};
	this->init();
}

JobCreatePlot::JobCreatePlot(std::string title, std::string originalTitle) : Job(title, originalTitle)
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
		if (ImGui::CollapsingHeader("Plot Parameters")) {
			ImGui::TextWrapped("changing these values, won\'t affect running process, if the job is relaunched, it will use these new parameters");
			ImGui::Indent(20.0f);
			result &= this->drawEditor();
			ImGui::Unindent(20.0f);
		}		
	}
	return result;
}

bool JobCreatePlot::relaunchAfterFinish()
{
	return this->finishRule.relaunchAfterFinish();
}

void JobCreatePlot::init()
{
	this->startEvent = std::make_shared<JobEvent>("job-start", this->getOriginalTitle());
	this->finishEvent = std::make_shared<JobEvent>("job-finish", this->getOriginalTitle());
	this->phase1FinishEvent = std::make_shared<JobEvent>("phase1-finish", this->getOriginalTitle());
	this->phase2FinishEvent = std::make_shared<JobEvent>("phase2-finish", this->getOriginalTitle());
	this->phase3FinishEvent = std::make_shared<JobEvent>("phase3-finish", this->getOriginalTitle());
	this->phase4FinishEvent = std::make_shared<JobEvent>("phase4-finish", this->getOriginalTitle());
	this->events.push_back(this->startEvent);
	this->events.push_back(this->finishEvent);
	this->events.push_back(this->phase1FinishEvent);
	this->events.push_back(this->phase2FinishEvent);
	this->events.push_back(this->phase3FinishEvent);
	this->events.push_back(this->phase4FinishEvent);
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


