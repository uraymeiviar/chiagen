#include "Job.hpp"
#include "imgui.h"
#include <thread>

#include <psapi.h>

JobManager& JobManager::getInstance() {
	static JobManager instance;
	return instance;
}

void JobManager::triggerEvent(std::shared_ptr<JobEvent> jobEvent)
{
	for (auto job : this->activeJobs) {
		try {
			job->handleEvent(jobEvent);
		}
		catch (...) {

		}
	}
}

void JobManager::listEvents(std::vector<std::shared_ptr<JobEvent>> out)
{
	for (auto job : this->activeJobs) {
		for (auto jobEvent : job->events) {
			out.push_back(jobEvent);
		}
	}
}

void JobManager::addJob(std::shared_ptr<Job> newJob) {
	this->activeJobs.push_back(newJob);
}

void JobManager::setSelectedJob(std::shared_ptr<Job> job) {
	this->selectedJob = job;
}

std::shared_ptr<Job> JobManager::getSelectedJob() const {
	return this->selectedJob;
}

size_t JobManager::countJob() const {
	return this->activeJobs.size();
}

size_t JobManager::countRunningJob() const {
	size_t result = 0;
	for (auto job : this->activeJobs) {
		if (job->isRunning()) {
			result++;
		}
	}
	return result;
}

std::vector<std::shared_ptr<Job>>::const_iterator JobManager::jobIteratorBegin() const  {
	return this->activeJobs.begin();
}

std::vector<std::shared_ptr<Job>>::const_iterator JobManager::jobIteratorEnd() const  {
	return this->activeJobs.end();
}

Job::Job(std::string title) :title(title)
{

}

std::string Job::getTitle() const { return this->title; }

void Job::drawItemWidget() {
	ImGui::Text(this->getTitle().c_str());
	ImGui::ProgressBar(this->getProgress());
}

void Job::drawStatusWidget() {
	ImGui::Text(this->getTitle().c_str());
	ImGui::ProgressBar(this->getProgress());
	if (this->isRunning()) {
		if(this->isPaused()){
			if (ImGui::Button("Resume")) {
				this->start();
			}
		}
		else {
			if (ImGui::Button("Pause")) {
				this->pause();
			}
		}
	}
	else {
		if (this->getStartRule().evaluate()) {
			if (ImGui::Button("Start")) {
				this->start();
			}
		}
		else {
			if (ImGui::Button("Overide Start")) {
				this->start();
			}
		}
			
	}
	ImGui::SameLine();
	if (this->isRunning()) {
		if (ImGui::Button("Cancel")) {
			this->cancel();
		}
	}
	else {
		if (ImGui::Button("Delete")) {
			this->cancel();
		}
	}
}

JobTaskItem::JobTaskItem(std::string name) :name(name)
{

}

void JobTaskItem::addChild(std::shared_ptr<JobTaskItem> task)
{
	if (task->parentTask != this->shared_from_this()) {
		this->tasks.push_back(task);
		if (task->parentTask) {
			task->parentTask->removeChild(task);
		}
		task->parentTask = this->shared_from_this();
	}
}

void JobTaskItem::removeChild(std::shared_ptr<JobTaskItem> task)
{
	this->tasks.erase(std::remove_if(this->tasks.begin(), this->tasks.end(),
		[=](std::weak_ptr<JobTaskItem> child) {
			if (child.lock() == task) {
				task->parentTask = nullptr;
				return true;
			}
			else {
				return false;
			}
		}
	),this->tasks.end());
}

float JobTaskItem::getProgress()
{
	if (!this->tasks.empty()) {
		float childProgress = 0.0f;
		float childWeight = 1.0f/(float)this->tasks.size();
		for (auto child : this->tasks) {
			auto childPtr = child.lock();
			if (childPtr) {
				childProgress += childPtr->getProgress()*childWeight;
			}			
		}
		if (childProgress > 1.0f) {
			childProgress = 1.0f;
		}
		if (childProgress < 0.0f) {
			childProgress = 0.0f;
		}
		return childProgress;
	}
	else {
		float val = 0.0f;
		uint32_t selfWorkItem = this->getCompletedWorkItems();
		if (selfWorkItem > 0) {
			val = (float)this->getCompletedWorkItems() / (float)selfWorkItem;
		}
		if (val > 1.0f) {
			val = 1.0f;
		}
		if (val < 0.0f) {
			val = 0.0f;
		}
		return val;
	}
}

uint32_t JobTaskItem::getTotalWorkItems()
{
	uint32_t childTotal = 0;
	for (auto child : this->tasks) {
		auto childPtr = child.lock();
		if (childPtr) {
			childTotal += childPtr->getTotalWorkItems();
		}		
	}
	return childTotal + this->totalWorkItem;
}

uint32_t JobTaskItem::getCompletedWorkItems()
{
	uint32_t childTotal = 0;
	for (auto child : this->tasks) {
		auto childPtr = child.lock();
		if (childPtr) {
			childTotal += childPtr->getCompletedWorkItems();
		}		
	}
	return childTotal + this->completedWorkItem;
}

void JobTaskItem::start()
{
	if (!this->state.running) {
		this->startTime = std::chrono::system_clock::now();
		this->state.running = true;
	}
	this->state.finished = false;
}

void JobTaskItem::stop(bool finished /*= true*/)
{
	if (finished) {
		this->finishTime = std::chrono::system_clock::now();
		this->state.running = false;
	}
	this->state.finished = finished;
}

bool JobTaskItem::isRunning() const
{
	return this->state.running;
}

bool JobTaskItem::isFinished() const
{
	return this->state.finished;
}

JobActvity::JobActvity(std::string name) :JobTaskItem(name)
{
	this->myProcess = ::GetCurrentProcess();
}

void JobActvity::start(uint32_t updateInterval, uint32_t sampleCount)
{
	this->statSampleCount = sampleCount;
	this->statUpdateInterval = updateInterval;	
	this->lastSampleTime = std::chrono::steady_clock::now();
	//TODO : start jobThread
	//if success
	this->state.paused = false;
	JobTaskItem::start();
}

void JobActvity::stop(bool finished, bool force /*= false*/)
{
	this->state.finished = finished;
	samplerUpdate();
	this->state.running = false;
	if (!finished) {
		//TODO:: kill thread ? or wait?
	}
	JobTaskItem::stop(finished);
}

void JobActvity::pause(bool isPause /*= true*/)
{
	if (isPause != this->state.paused) {
		if (!isPause) {
			this->state.paused = true;
			this->lastSampleTime = std::chrono::steady_clock::now();
			//TODO : suspend jobThread
		}
		else {
			this->state.paused = false;
			this->lastSampleTime = std::chrono::steady_clock::now();
			//TODO : resume jobThread
		}
	}
}

bool JobActvity::isPaused() const
{
	return this->state.paused;
}

void JobActvity::samplerUpdate()
{
	this->collectCPUUsage();
	this->collectMemUsage();
	this->collectDiskUsage();
}

void JobActvity::collectCPUUsage()
{
	std::chrono::time_point<std::chrono::steady_clock> cur = std::chrono::steady_clock::now();
	FILETIME creationTime;
	FILETIME exitTime;
	FILETIME kernelTime;
	FILETIME userTime;
	::GetThreadTimes(this->jobThread.native_handle(), &creationTime, &exitTime, &kernelTime, &userTime);
	ULARGE_INTEGER kernelTimeInt;
	kernelTimeInt.LowPart = kernelTime.dwLowDateTime;
	kernelTimeInt.HighPart = kernelTime.dwHighDateTime;
	uint64_t intervalKernelTime = this->totalKernelTime - kernelTimeInt.QuadPart;
	this->totalKernelTime = kernelTimeInt.QuadPart;

	ULARGE_INTEGER userTimeInt;
	userTimeInt.LowPart = userTime.dwLowDateTime;
	userTimeInt.HighPart = userTime.dwHighDateTime;
	uint64_t intervalUserTime = this->totalUserTime - userTimeInt.QuadPart;
	this->totalUserTime = kernelTimeInt.QuadPart;

	uint64_t intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		this->lastSampleTime - cur).count();
	float cpuKernelTime = (float)(((double)intervalKernelTime / 10000.0) / (double)intervalMs);
	float cpuUserTime = (float)(((double)intervalUserTime / 10000.0) / (double)intervalMs);

	this->cpuKernelTimeHistory.push_back(cpuKernelTime);
	this->cpuUserTimeHistory.push_back(cpuUserTime);

	while (this->cpuKernelTimeHistory.size() > this->statSampleCount) {
		this->cpuKernelTimeHistory.erase(this->cpuKernelTimeHistory.begin());
	}

	while (this->cpuUserTimeHistory.size() > this->statSampleCount) {
		this->cpuUserTimeHistory.erase(this->cpuUserTimeHistory.begin());
	}
	this->lastSampleTime = std::chrono::steady_clock::now();
}

void JobActvity::collectMemUsage()
{
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(this->myProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
	{
		this->memUsageHistory.push_back(pmc.PrivateUsage);
		while (this->memUsageHistory.size() > this->statSampleCount) {
			this->memUsageHistory.erase(this->memUsageHistory.begin());
		}
	}
}

void JobActvity::collectDiskUsage()
{
	::IO_COUNTERS counter;
	if (::GetProcessIoCounters(this->myProcess, &counter)) {
		this->diskWriteHistory.push_back(counter.WriteTransferCount - lastDiskWrite);
		this->lastDiskWrite = counter.WriteTransferCount;
		this->diskReadHistory.push_back(counter.ReadTransferCount - lastDiskRead);
		this->lastDiskRead = counter.ReadTransferCount;

		while (this->diskWriteHistory.size() > this->statSampleCount) {
			this->diskWriteHistory.erase(this->diskWriteHistory.begin());
		}

		while (this->diskReadHistory.size() > this->statSampleCount) {
			this->diskReadHistory.erase(this->diskReadHistory.begin());
		}
	}
}

void JobActvity::samplerThreadProc()
{
	while (this->isRunning()) {
		if (!this->isPaused()) {
			auto start = std::chrono::steady_clock::now();
			auto elapsed = start - this->lastSampleTime;
			if (elapsed > std::chrono::seconds(this->statUpdateInterval)) {
				this->samplerUpdate();
				this->lastSampleTime = start;
			}
			else {
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
}

JobEvent::JobEvent(std::string type) : type(type)
{

}

void JobEvent::trigger()
{
	JobManager::getInstance().triggerEvent(this->shared_from_this());
}

std::string JobEvent::getType() const
{
	return this->type;
}
