#include "Job.hpp"
#include "imgui.h"


#include <psapi.h>

JobManager& JobManager::getInstance() {
	static JobManager instance;
	return instance;
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

JobTaskItem::JobTaskItem(std::string name, std::shared_ptr<JobTaskItem> parent) :name(name), parentTask(parent)
{

}

void JobTaskItem::addDiskRead(uint64_t byteSize)
{
	if (this->parentTask) {
		parentTask->addDiskRead(byteSize);
	}
}

void JobTaskItem::addDiskWrite(uint64_t byteSize)
{
	if (this->parentTask) {
		parentTask->addDiskWrite(byteSize);
	}
}

float JobTaskItem::getProgress()
{
	if (!this->tasks.empty()) {
		float childProgress = 0.0f;
		float childWeight = 1.0f/(float)this->tasks.size();
		for (auto child : this->tasks) {
			childProgress += child->getProgress()*childWeight;
		}
		if (childProgress > 1.0f) {
			return 1.0f;
		}
		if (childProgress < 0.0f) {
			return 0.0f;
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
			return 1.0f;
		}
		if (val < 0.0f) {
			return 0.0f;
		}
	}
}

uint32_t JobTaskItem::getTotalWorkItems()
{
	uint32_t childTotal = 0;
	for (auto child : this->tasks) {
		childTotal += child->getTotalWorkItems();
	}
	return childTotal + this->totalWorkItem;
}

uint32_t JobTaskItem::getCompletedWorkItems()
{
	uint32_t childTotal = 0;
	for (auto child : this->tasks) {
		childTotal += child->getCompletedWorkItems();
	}
	return childTotal + this->completedWorkItem;
}

void JobTaskItem::start()
{
	if (!this->running) {
		this->startTime = std::chrono::system_clock::now();
		this->running = true;
	}
	this->finished = false;
}

void JobTaskItem::stop(bool finished /*= true*/)
{
	if (finished) {
		this->finishTime = std::chrono::system_clock::now();
		this->running = false;
	}
	this->finished = finished;
}

bool JobTaskItem::isRunning() const
{
	return this->running;
}

bool JobTaskItem::isFinished() const
{
	return this->finished;
}

JobProgress::JobProgress(std::string name) :JobTaskItem(name, nullptr)
{
	this->myProcess = ::GetCurrentProcess();
}

void JobProgress::addDiskRead(uint64_t byteSize)
{
	this->diskRead += byteSize;
}

void JobProgress::addDiskWrite(uint64_t byteSize)
{
	this->diskWrite += byteSize;
}

void JobProgress::start(HANDLE jobThread, uint32_t updateInterval, uint32_t sampleCount)
{
	this->statSampleCount = sampleCount;
	this->statUpdateInterval = updateInterval;	
	this->lastSampleTime = std::chrono::steady_clock::now();
	//TODO : start jobThread
	//if success
	this->paused = false;
	JobTaskItem::start();
}

void JobProgress::stop(bool finished, bool force /*= false*/)
{
	this->finished = finished;
	samplerUpdate();
	this->running = false;
	if (!finished) {
		//TODO:: kill thread ? or wait?
	}
	JobTaskItem::stop(finished);
}

void JobProgress::pause(bool isPause /*= true*/)
{
	if (isPause != this->paused) {
		if (!isPause) {
			this->paused = true;
			this->lastSampleTime = std::chrono::steady_clock::now();
			//TODO : suspend jobThread
		}
		else {
			this->paused = false;
			this->lastSampleTime = std::chrono::steady_clock::now();
			//TODO : resume jobThread
		}
	}
}

bool JobProgress::isPaused() const
{
	return this->paused;
}

void JobProgress::samplerUpdate()
{
	this->collectCPUUsage();
	this->collectMemUsage();
	this->collectDiskUsage();
}

void JobProgress::collectCPUUsage()
{
	std::chrono::time_point<std::chrono::steady_clock> cur = std::chrono::steady_clock::now();
	FILETIME creationTime;
	FILETIME exitTime;
	FILETIME kernelTime;
	FILETIME userTime;
	::GetThreadTimes(this->jobThread, &creationTime, &exitTime, &kernelTime, &userTime);
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

void JobProgress::collectMemUsage()
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

void JobProgress::collectDiskUsage()
{
	this->diskWriteHistory.push_back(this->lastDiskWrite - this->diskWrite);
	this->lastDiskWrite = this->diskWrite;
	this->diskReadHistory.push_back(this->lastDiskRead - this->diskRead);
	this->lastDiskRead = this->diskRead;

	while (this->diskWriteHistory.size() > this->statSampleCount) {
		this->diskWriteHistory.erase(this->diskWriteHistory.begin());
	}

	while (this->diskReadHistory.size() > this->statSampleCount) {
		this->diskReadHistory.erase(this->diskReadHistory.begin());
	}
}
