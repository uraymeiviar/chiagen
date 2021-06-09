#include "Job.hpp"
#include "imgui.h"

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