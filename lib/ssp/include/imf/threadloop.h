#pragma once

#include <functional>
#include <thread>
#include <memory>

#include "constructormagic.h"
#include "ISspClient.h"

namespace imf {

class ThreadLoop {
public:
	typedef std::function<void(Loop *)> PreLoopCallback;
	typedef std::function<ILoop_class *(void)> LoopCreater;
	explicit ThreadLoop(const PreLoopCallback &pre = PreLoopCallback(),
			    const LoopCreater &loop = LoopCreater())
		: preLoopCb_(pre),
		  create_loop(loop),
		  loop_(nullptr),
		  started_(false)
	{
	}

	~ThreadLoop()
	{
		// Buggy
		if (started_ && loop_) {
			loop_->quit();
			thread_.join();
		}
		if (loop_) {
			loop_->destroy();
		}
	}

	void start(void)
	{
		if (started_ == false) {
			started_ = true;
			thread_ = std::thread(&ThreadLoop::run, this);
		}
	}

	void stop(void)
	{
		started_ = false;
		loop_->quit();
		thread_.join();
	}

private:
	void run(void)
	{
		if (loop_) {
			loop_->destroy();
		}
		loop_ = create_loop();
		loop_->init();

		if (preLoopCb_) {
			preLoopCb_((Loop *)loop_->getLoop());
		}
		loop_->loop();
	}

	PreLoopCallback preLoopCb_;
	bool started_;
	std::thread thread_;
	ILoop_class *loop_;
	LoopCreater create_loop;

	DISALLOW_COPY_AND_ASSIGN(ThreadLoop);
};
}