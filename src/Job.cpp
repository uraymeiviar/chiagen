#include "Job.hpp"
#include "imgui.h"
#include <thread>
#include <future>
#include <psapi.h>
#include "util.hpp"
#include "Implot/implot.h"
#include "JobRule.h"

std::vector<std::function<void()>> JobManager::registrations = std::vector<std::function<void()>>();

JobManager::JobManager()
{
	this->myProcess = NULL;
}

JobManager& JobManager::getInstance() {
	static JobManager instance;
	return instance;
}

void JobManager::triggerEvent(std::shared_ptr<JobEvent> jobEvent,std::shared_ptr<Job> source)
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	for (auto job : this->activeJobs) {
		try {
			if (source && job != source) {
				job->handleEvent(jobEvent, source);
			}			
		}
		catch (...) {

		}
	}
}

void JobManager::listEvents(std::vector<std::shared_ptr<JobEvent>> out)
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	for (auto job : this->activeJobs) {
		for (auto jobEvent : job->events) {
			out.push_back(jobEvent);
		}
	}
}

void JobManager::addJob(std::shared_ptr<Job> newJob) {
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	this->activeJobs.push_back(newJob);
}

void JobManager::deleteJob(std::shared_ptr<Job> newJob)
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	this->deleteReqsJobs.push_back(newJob);
}

void JobManager::setSelectedJob(std::shared_ptr<Job> job) {
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	this->selectedJob = job;
}

std::shared_ptr<Job> JobManager::getSelectedJob() {
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	return this->selectedJob;
}

std::shared_ptr<Job> JobManager::getActiveJobByName(std::string name, bool originalName)
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	for (auto job : this->activeJobs) {
		if (originalName) {
			if (job->getOriginalTitle() == name) {
				return job;
			}
		}
		else if (job->getTitle() == name) {
			return job;
		}
	}
	return nullptr;
}

size_t JobManager::countJob() {
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	return this->activeJobs.size();
}

size_t JobManager::countRunningJob() {
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	size_t result = 0;
	for (auto job : this->activeJobs) {
		if (job->isRunning()) {
			result++;
		}
	}
	return result;
}

std::vector<std::shared_ptr<Job>> JobManager::getActiveJobs()
{
	std::vector<std::shared_ptr<Job>> result;
	for (auto a : this->activeJobs) {
		result.push_back(a);
	}
	for (auto a : this->finishedJobs) {
		result.push_back(a);
	}
	return result;
}

void JobManager::collectMemUsage()
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

void JobManager::collectDiskUsage()
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

void JobManager::collectPerfSample()
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	this->collectMemUsage();
	this->collectDiskUsage();	
}

void JobManager::samplerThreadProc()
{
	while (JobManager::getInstance().isRunning) {
		auto start = std::chrono::steady_clock::now();
		auto elapsed = start - this->lastSampleTime;
		if (elapsed > std::chrono::seconds(this->statUpdateInterval)) {
			JobManager::getInstance().lastSampleTime = start;
			JobManager::getInstance().collectPerfSample();
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

void JobManager::registerJobFactory(std::shared_ptr<JobFactory> factory)
{
	this->jobFactories.push_back(factory);
}

void JobManager::stop()
{
	for (auto job : JobManager::getInstance().activeJobs) {
		job->cancel(true);
	}
	this->isRunning = false;
	if (this->samplerThread.joinable()) {
		this->samplerThread.join();
	}
}

void JobManager::start()
{
	if (this->myProcess == NULL) {
		this->myProcess = ::GetCurrentProcess();
		if (this->myProcess != NULL) {
			this->isRunning = true;
			this->samplerThread = std::thread(&JobManager::samplerThreadProc, &JobManager::getInstance());
		}
	}
	for (auto reg : JobManager::registrations) {
		reg();
	}
	JobManager::registrations.clear();
}

void JobManager::update()
{
	const std::lock_guard<std::recursive_mutex> lock(this->mutex);
	while(!deleteReqsJobs.empty()) {
		std::shared_ptr<Job> jobToDelete = deleteReqsJobs.at(0);
		this->activeJobs.erase(
			std::remove_if(
				this->activeJobs.begin(), 
				this->activeJobs.end(),
				[=](auto job) {
					if (this->selectedJob == job) {
						this->selectedJob = nullptr;
					}
					return job == jobToDelete;
				}
			)
		);
		if (!this->finishedJobs.empty()) {
			this->finishedJobs.erase(
				std::remove_if(
					this->finishedJobs.begin(), 
					this->finishedJobs.end(),
					[=](auto job) {
						if (this->selectedJob == job) {
							this->selectedJob = nullptr;
						}
						return job == jobToDelete;
					}
				)
			);
		}
		deleteReqsJobs.erase(deleteReqsJobs.begin());
	}

	while(!relaunchReqsJobs.empty()) {
		std::shared_ptr<Job> jobToRelaunch = relaunchReqsJobs.at(0);
		std::shared_ptr<Job> relaunchedJob = jobToRelaunch->relaunch();
		if (relaunchedJob) {
			JobManager::getInstance().addJob(relaunchedJob);
		}
		if (this->selectedJob == jobToRelaunch) {
			this->selectedJob = nullptr;
		}
		relaunchReqsJobs.erase(relaunchReqsJobs.begin());
	}

	this->finishedJobs.erase(
		std::remove_if(
			this->finishedJobs.begin(), 
			this->finishedJobs.end(),
			[=](auto job) {
				if (job->relaunchAfterFinish()) {
					this->relaunchReqsJobs.push_back(job);
					return true;
				}
				else {
					if (job->update() && !job->isFinished()) {
						this->activeJobs.push_back(job); 
						return true;
					}
					return false;
				}
			}
		),
		this->finishedJobs.end()
	);

	std::vector<std::shared_ptr<Job>> tempFinishedJobs;
	for (auto job : JobManager::getInstance().activeJobs) {
		bool keepAlive = job->update();
		if (!keepAlive) {
			tempFinishedJobs.push_back(job);
		}
	}
	for (auto job : tempFinishedJobs) {
		finishedJobs.push_back(job);
		this->activeJobs.erase(
			std::remove_if(
				this->activeJobs.begin(), 
				this->activeJobs.end(),
				[=](auto activeJob) {
					return activeJob == job;
				}
			),
			this->activeJobs.end()
		);
	}
}

void JobManager::log(std::string text, std::shared_ptr<Job> job)
{
	const std::lock_guard<std::recursive_mutex> lock(this->logMutex);
	std::string prefix = "[] ";
	if (job) {
		prefix = "[" + job->getTitle() + "] ";
	}
	std::string clockStr = "["+systemClockToStr(std::chrono::system_clock::now())+"] ";
	std::cout << prefix + clockStr + text << std::endl;
	if (job) {
		job->log(clockStr + text);
	}
}

void JobManager::logErr(std::string text, std::shared_ptr<Job> job /*= nullptr*/)
{
	const std::lock_guard<std::recursive_mutex> lock(this->logMutex);
	std::string prefix = "[] ";
	if (job) {
		prefix = "[" + job->getTitle() + "] ";
	}
	std::string clockStr = systemClockToStr(std::chrono::system_clock::now());
	std::cerr << prefix + clockStr + " " + text << std::endl;
	if (job) {
		job->log(clockStr + text);
	}
}

std::vector<std::string> JobManager::getAvailableEventTypes()
{
	const std::lock_guard<std::recursive_mutex> lock(this->logMutex);
	std::vector<std::string> eventNames;
	for (auto activeJob : this->activeJobs) {
		for (auto jobEvent : activeJob->events) {
			if (std::find(eventNames.begin(), eventNames.end(), jobEvent->getType()) == eventNames.end()) {
				eventNames.push_back(jobEvent->getType());
			}
		}
	}
	return eventNames;
}

std::vector<std::string> JobManager::getAvailableEventEmitters()
{
	const std::lock_guard<std::recursive_mutex> lock(this->logMutex);
	std::vector<std::string> jobNames;
	for (auto activeJob : this->activeJobs) {
		if (!activeJob->events.empty()) {
			jobNames.push_back(activeJob->getOriginalTitle());
		}
	}
	return jobNames;
}

Job::Job(std::string title, std::string originalTitle) :title(title), originalTitle(originalTitle)
{
	if (originalTitle.empty()) {
		originalTitle = title;
	}
}

Job::~Job()
{
	this->activity = nullptr;
}

bool Job::isRunning() const
{
	if (this->activity) {
		return this->activity->isRunning();
	}
	else {
		return false;
	}
}

bool Job::isPaused() const
{
	if (this->activity) {
		return this->activity->isPaused();
	}
	else {
		return false;
	}
}

bool Job::isFinished() const
{
	if (this->activity) {
		return this->activity->isFinished();
	}
	else {
		return false;
	}
}

bool Job::start(bool overrideRule)
{
	bool doStart = false;
	if (!overrideRule) {
		JobRule* startRule = this->getStartRule();
		if (startRule) {
			doStart = startRule->evaluate();
		}
		else {
			doStart = true;
		}		
	}
	else {
		doStart = true;
	}
	
	if (doStart && !this->isRunning()) {
		this->initActivity();
		if (this->activity) {
			return this->activity->start();
		}
	}	
	return false;
}

bool Job::pause()
{
	if (this->activity) {
		return this->activity->pause();
	}
	else {
		return false;
	}
}

bool Job::cancel(bool forced)
{
	if (!this->isRunning()) {
		JobManager::getInstance().deleteJob(this->shared_from_this());
		return true;
	}
	else {
		if (this->activity) {
			return this->activity->stop(false, forced);
		}
		else {
			JobManager::getInstance().deleteJob(this->shared_from_this());
			return true;
		}
	}		
}

float Job::getProgress()
{
	if (this->activity) {
		return this->activity->getProgress();
	}
	else {
		return 0.0f;;
	}
}

JobRule* Job::getStartRule()
{
	return nullptr;
}

JobRule* Job::getFinishRule()
{
	return nullptr;
}

std::string Job::getTitle() const { return this->title; }

std::string Job::getOriginalTitle() const
{
	return this->originalTitle;
}

bool Job::drawEditor()
{
	return false;
}

bool Job::drawItemWidget() {
	ImGui::Text(this->getTitle().c_str());
	ImGui::ProgressBar(this->getProgress());
	return false;
}

bool Job::drawStatusWidget() {
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
		JobRule* startRule = this->getStartRule();
		if (startRule) {
			bool evalResult = startRule->evaluate();
			if (evalResult && ImGui::Button("Start")) {
				this->start(false);
			}
			if (!evalResult) {
				if (ImGui::Button("Overide Start")) {
					this->start(true);
				}
			}
		}
		else {
			if (ImGui::Button("Start")) {
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
	if (ImGui::CollapsingHeader("Logs")) {
		if(ImGui::BeginChild("##Log",ImVec2(0,180.0f),false,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)){
			ImGui::TextWrapped(this->logText.c_str());
			if (this->logScroll){
				ImGui::SetScrollHere(1.0f);
			}
			this->logScroll = false;
		}
		ImGui::EndChild();
	}
	return false;
}

void Job::handleEvent(std::shared_ptr<JobEvent> jobEvent, std::shared_ptr<Job> source)
{
	JobRule* startRule = this->getStartRule();
	JobRule* finishRule = this->getFinishRule();
	if (startRule) {
		startRule->handleEvent(jobEvent, source);
	}
	if (finishRule) {
		finishRule->handleEvent(jobEvent, source);
	}
}

bool Job::update()
{
	if (!this->isRunning()) {
		if (!this->isFinished()) {
			JobRule* startRule = this->getStartRule();
			if (startRule) {
				startRule->update();
				if (startRule->evaluate()) {
					this->start(false);
				}
			}			
			return true;
		}
		else {
			//this->activity = nullptr;
			JobRule* finishRule = this->getFinishRule();
			if (finishRule) {
				finishRule->update();
			}
			return false;
		}
		return true;
	}
	else {
		if (this->activity) {
			this->activity->samplerUpdate();
		}
		return true;
	}
}

bool Job::relaunchAfterFinish()
{
	return false;
}

void Job::log(std::string text)
{
	this->logText += text +"\n";
	this->logScroll = true;
}

void Job::initActivity()
{
	this->activity = std::make_shared<JobActvity>(this->getTitle(), this);
}

JobTaskItem::JobTaskItem(std::string name) :name(name)
{

}

JobTaskItem::~JobTaskItem()
{
	std::cout << "task item destroy" << std::endl;
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
	if (this->isFinished()) {
		return 1.0f;
	}
	if (!this->tasks.empty()) {
		float childProgress = 0.0f;
		float childWeight = 1.0f/(float)this->tasks.size();
		for (auto child : this->tasks) {
			childProgress += child->getProgress()*childWeight;		
		}
		if (childProgress > 1.0f) {
			childProgress = 1.0f;
		}
		if (childProgress < 0.0f) {
			childProgress = 0.0f;
		}
		if (!this->isFinished() && childProgress >= 0.999) {
			childProgress = 0.99;
		}
		return childProgress;
	}
	else {
		float val = 0.0f;
		uint32_t selfWorkItem = this->getTotalWorkItems();
		if (selfWorkItem > 0) {
			val = (float)this->getCompletedWorkItems() / (float)selfWorkItem;
		}
		if (val > 1.0f) {
			val = 1.0f;
		}
		if (val < 0.0f) {
			val = 0.0f;
		}
		if (!this->isFinished() && val >= 0.999) {
			val = 0.99;
		}
		return val;
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

bool JobTaskItem::start()
{
	if (!this->state.running && !this->state.finished) {
		this->startTime = std::chrono::system_clock::now();
		this->state.running = true;
		this->state.finished = false;
		return true;
	}
	return false;
}

bool JobTaskItem::stop(bool finished /*= true*/)
{
	if (finished) {
		this->finishTime = std::chrono::system_clock::now();	
	}
	this->state.running = false;
	this->state.finished = finished;
	return true;
}

bool JobTaskItem::isRunning() const
{
	return this->state.running;
}

bool JobTaskItem::isFinished() const
{
	return this->state.finished;
}

bool JobTaskItem::drawStatusWidget()
{	
	if (this->isRunning()) {
		ImGui::SetNextItemOpen(true);
	}
	if (ImGui::TreeNode(this->name.c_str())) {
		ImGui::Indent(8);
		if (this->isFinished()) {
			ImGui::Text("Start %s",systemClockToStr(this->startTime).c_str());
			ImGui::Text("Finished %s",systemClockToStr(this->finishTime).c_str());
			auto dt = std::chrono::duration_cast<std::chrono::seconds>(this->finishTime - this->startTime);
			ImGui::Text("Elapsed %.1f minutes",dt.count()/60.0f);
		}
		else if (this->isRunning()) {				
			ImGui::Text("Start %s",systemClockToStr(this->startTime).c_str());
			std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
			auto dt = std::chrono::duration_cast<std::chrono::seconds>(currentTime - this->startTime);
			ImGui::Text("Elapsed %.1f minutes",dt.count()/60.0f);
		}
		else {
			ImGui::Text("Not Started");
		}
		if (this->isRunning()) {
			ImGui::ProgressBar(this->getProgress());
		}
		for (auto task : this->tasks) {
			task->drawStatusWidget();
		}
		ImGui::Unindent(8);
		ImGui::TreePop();
	}
	return false;
}

JobActvity::JobActvity(std::string name, Job* owner) : JobTaskItem(name), job(owner)
{
	this->cpuKernelTimeHistory = std::vector<float>(100);
	this->cpuUserTimeHistory = std::vector<float>(100);
	for (size_t i = 0; i < 100; i++) {
		this->xAxis.push_back(i+1);
	}
}

JobActvity::~JobActvity()
{
	if (this->jobThread.joinable()) {
		this->jobThread.join();
	}
	std::cout << "activity quit " << std::endl;
}

bool JobActvity::start(uint32_t sampleCount)
{
	this->statSampleCount = sampleCount;
	if (!this->isFinished() && !this->isRunning() && this->mainRoutine) {
		this->lastSampleTime = std::chrono::steady_clock::now();
		if (JobTaskItem::start()) {
			this->jobThread = std::thread([=](){
				try {
					this->mainRoutine(&this->state);
				}
				catch (...) {

				}
				this->stopActivity(true);
			});
			if (this->jobThread.joinable()) {
				return true;
			}
		}
		else {
			return false;
		}
	}
	return false;
}

bool JobActvity::start()
{
	return this->start(100);
}

//should NOT be called inside JobThread !!
bool JobActvity::stop(bool finished, bool force /*= false*/)
{
	if (this->isRunning()) {
		if (this->stopActivity(finished)) {
			if (force) {
				auto future = std::async(std::launch::async, &std::thread::join, &this->jobThread);
				if (future.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) {
					::TerminateThread(this->jobThread.native_handle(),0);
					if (finished && this->onFinish) {
						this->onFinish(&this->state);
					}
				}
				else {
					if (finished && this->onFinish) {
						this->onFinish(&this->state);
					}
				}
			}
			else {
				if (this->jobThread.joinable()) {
					this->jobThread.join();
				}
				if (finished && this->onFinish) {
					this->onFinish(&this->state);
				}
			}
			return true;
		}
		else {
			return false;
		}
	}
	else {
		return false;
	}
}

void JobActvity::drawPlot()
{
	const std::lock_guard<std::mutex> lock(this->mutex);
	if (!this->cpuUserTimeHistory.empty() && !this->cpuKernelTimeHistory.empty()) {
		float fieldWidth = ImGui::GetWindowContentRegionWidth();
		ImPlot::SetNextPlotLimits(0,100,0,100);
		if(ImPlot::BeginPlot("CPU Usage", nullptr, nullptr,ImVec2(fieldWidth,180.0f),ImPlotFlags_CanvasOnly,ImPlotAxisFlags_NoDecorations|ImPlotAxisFlags_Lock,ImPlotAxisFlags_NoDecorations|ImPlotAxisFlags_Lock)) {
			ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
			ImPlot::PlotLine("User CPU Time",this->xAxis.data(),this->cpuUserTimeHistory.data(),this->cpuUserTimeHistory.size());
			ImPlot::PlotLine("kernel CPU Time",this->xAxis.data(),this->cpuKernelTimeHistory.data(),this->cpuKernelTimeHistory.size());
			ImPlot::PopStyleVar();
			ImPlot::EndPlot();
		}
	}
}

bool JobActvity::stopActivity(bool finished)
{
	this->samplerUpdate();
	return JobTaskItem::stop(finished);
}

bool JobActvity::stop(bool finished /*= true*/)
{
	return this->stop(finished, false);
}

bool JobActvity::pause(bool isPause /*= true*/)
{
	if (isPause != this->state.paused) {
		if (!isPause) {			
			this->lastSampleTime = std::chrono::steady_clock::now();
			if (::SuspendThread(this->jobThread.native_handle()) >= 0) {
				this->state.paused = true;
				if (this->onPause) {
					this->onPause(&this->state);
				}
				return true;
			}
		}
		else {
			this->state.paused = false;
			this->lastSampleTime = std::chrono::steady_clock::now();
			if (::ResumeThread(this->jobThread.native_handle()) >= 0) {
				this->state.paused = false;
				if (this->onResume) {
					this->onResume(&this->state);
				}
				return true;
			}
		}
		return true;
	}
	return false;
}

bool JobActvity::isPaused() const
{
	return this->state.paused;
}

bool JobActvity::drawStatusWidget()
{
	if (!this->tasks.empty()) {
		//this->drawPlot();
		if (this->isFinished()) {
			ImGui::Text("Start %s",systemClockToStr(this->startTime).c_str());
			ImGui::Text("Finished %s",systemClockToStr(this->finishTime).c_str());
			auto dt = std::chrono::duration_cast<std::chrono::seconds>(this->finishTime - this->startTime);
			ImGui::Text("Elapsed %.1f minutes",dt.count()/60.0f);
		}
		else if (this->isRunning()) {				
			ImGui::Text("Start %s",systemClockToStr(this->startTime).c_str());
			std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();
			auto dt = std::chrono::duration_cast<std::chrono::seconds>(currentTime - this->startTime);
			ImGui::Text("Elapsed %.1f minutes",dt.count()/60.0f);
		}
		ImGui::Text("Sub Tasks");
		ImGui::Separator();
		for (auto task : this->tasks) {
			task->drawStatusWidget();
		}
	}
	return false;
}

void JobActvity::samplerUpdate()
{
	this->collectCPUUsage();
}

void JobActvity::waitUntilFinish()
{
	if (this->jobThread.joinable()) {
		this->jobThread.join();
	}
}

void JobActvity::collectCPUUsage()
{
	const std::lock_guard<std::mutex> lock(this->mutex);
	std::chrono::time_point<std::chrono::steady_clock> cur = std::chrono::steady_clock::now();
	FILETIME creationTime;
	FILETIME exitTime;
	FILETIME kernelTime;
	FILETIME userTime;
	::GetThreadTimes(this->jobThread.native_handle(), &creationTime, &exitTime, &kernelTime, &userTime);
	ULARGE_INTEGER kernelTimeInt;
	kernelTimeInt.LowPart = kernelTime.dwLowDateTime;
	kernelTimeInt.HighPart = kernelTime.dwHighDateTime;
	uint64_t intervalKernelTime = kernelTimeInt.QuadPart - this->totalKernelTime;
	this->totalKernelTime = kernelTimeInt.QuadPart;

	ULARGE_INTEGER userTimeInt;
	userTimeInt.LowPart = userTime.dwLowDateTime;
	userTimeInt.HighPart = userTime.dwHighDateTime;
	uint64_t intervalUserTime = userTimeInt.QuadPart - this->totalUserTime;
	this->totalUserTime = userTimeInt.QuadPart;

	uint64_t intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		cur - this->lastSampleTime).count();
	float cpuKernelTime = (float)(100.0f*((double)intervalKernelTime / 10000.0) / (double)intervalMs);
	float cpuUserTime   = (float)(100.0f*((double)intervalUserTime   / 10000.0) / (double)intervalMs);
	
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

JobEvent::JobEvent(std::string type, std::string name) : type(type), name(name)
{

}

void JobEvent::trigger(std::shared_ptr<Job> source)
{
	JobManager::getInstance().triggerEvent(this->shared_from_this(), source);
}

std::string JobEvent::getType() const
{
	return this->type;
}

bool JobEventId::isEmpty()
{
	return type.empty();
}

bool JobEventId::match(JobEvent* event)
{
	if (event) {
		if (this->type == event->getType()) {
			if (!this->name.empty()) {
				return this->name == event->name;
			}
			else {
				return true;
			}
		}
	}
	return false;
}
