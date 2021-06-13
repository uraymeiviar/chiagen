#include <string>
#include <chrono>
#include <memory>
#include "ImFrame/Include/ImFrame.h"
#include "Job.hpp"

class MainApp : public ImFrame::ImApp
{
public:
	MainApp(GLFWwindow * window);
	virtual ~MainApp() {}
	void OnUpdate() override;
protected:
	int activeTab {0};
	void toolPage();
	void statPage();
	void systemPage();
	void helpPage();
	int wx,wy,ww,wh;
};