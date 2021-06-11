#include "data.hpp"

std::shared_ptr<JobTaskItem> DiskPlotterContext::getCurrentTask()
{
	if (this->tasks.empty()) {
		return this->job;
	}
	else {
		return this->tasks.top();
	}
}

std::shared_ptr<JobTaskItem> DiskPlotterContext::pushTask(std::string name, uint32_t totalWorkItem)
{
	std::shared_ptr<JobTaskItem> newTask = std::make_shared<JobTaskItem>(name, this->getCurrentTask());
	newTask->totalWorkItem = totalWorkItem;
	this->getCurrentTask()->addChild(newTask);
	this->tasks.push(newTask);
	return newTask;
}

std::shared_ptr<JobTaskItem> DiskPlotterContext::popTask()
{
	std::shared_ptr<JobTaskItem> result = this->tasks.top();
	this->tasks.pop();
	return result;
}
