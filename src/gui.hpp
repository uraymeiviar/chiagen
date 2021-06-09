#ifndef CHIAGEN_GUI_H
#define CHIAGEN_GUI_H
#include <string>

class UiWidget {
public:
	UiWidget(){};
	virtual bool draw() = 0;
};

template<typename P> class Widget : UiWidget {
public:
	Widget():UiWidget(){};
	virtual void setData(P param) {
		this->param = param;
	}
	P getData() {
		return param;
	}
	const P getData() const {
		return param;
	}
protected:
	P param;
};

void tooltiipText(std::string txt, float padding_x = 4.0f, float padding_y = 2.0f);

#endif
