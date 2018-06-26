/*
Copyright (c) 2010-2012, Paul Houx - All rights reserved.
This code is intended for use with the Cinder C++ library: http://libcinder.org
Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
* Redistributions of source code must retain the above copyright notice, this list of conditions and
the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
the following disclaimer in the documentation and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/


#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinderHapVideoPlayer.h"



using namespace ci;
using namespace ci::app;
using namespace std;

class OFHapPlayer : public App {

public:

	static void prepare(Settings *settings);

	void				setup();
	void				shutdown();
	void				update();
	void				draw();

	void				mouseMove(MouseEvent event);
	void				mouseDown(MouseEvent event);
	void				mouseDrag(MouseEvent event);
	void				mouseUp(MouseEvent event);

	void				keyDown(KeyEvent event);
	void				keyUp(KeyEvent event);

private:

	bool mMoviePlaying = false;
	cinderHapVideoPlayer* mPlayer;

	float currentFps = 0.0f;

};

void OFHapPlayer::prepare(Settings *settings) {
	settings->setWindowSize(640, 480);
	//settings->getDefaultRenderer;
	settings->getDefaultRenderer();
	settings->setFrameRate(60.0f);
	settings->setTitle("Picking using multiple targets and color coding");
}

void OFHapPlayer::setup() {
	mPlayer = new cinderHapVideoPlayer();	

}

void OFHapPlayer::shutdown() {

}

void OFHapPlayer::update() {
	currentFps = getAverageFps();
	mPlayer->update();
}



void OFHapPlayer::draw() {

	gl::clear(ColorA(0.2f, 0.2f, 0.2f));
	if (mPlayer->mTexture) {		
		mPlayer->draw();
	}

	gl::drawString(to_string(currentFps), vec2(50), ci::Color::white());
	gl::drawString(to_string(currentFps), vec2(50,75), ci::Color::black());

}

void OFHapPlayer::mouseMove(MouseEvent event) {
	
}

void OFHapPlayer::mouseDown(MouseEvent event) { 

}

void OFHapPlayer::mouseDrag(MouseEvent event) {
	
}

void OFHapPlayer::mouseUp(MouseEvent event) { }

void OFHapPlayer::keyDown(KeyEvent event)
{
	switch (event.getCode()) {
	case KeyEvent::KEY_ESCAPE:
		quit();
		break;
	case KeyEvent::KEY_SPACE:
		mPlayer->pause();
		break;
	case KeyEvent::KEY_f:
		setFullScreen(!isFullScreen());
		break;
	case KeyEvent::KEY_o:
		//#################################
		//CHANGE THE PATH TO THE VIDEO HERE
		//#################################
		//fs::path moviePath = "C:/videoTut-chapter2-11-HAPQ.avi";
		fs::path moviePath = "C:/sample-1080p30-HapA.avi";
		if (!moviePath.empty()) {
			mPlayer->open(moviePath.string());
			// StartGraph(moviePath);
		}
		mPlayer->play();
		break;

	}
		
}

void OFHapPlayer::keyUp(KeyEvent event) { }

CINDER_APP(OFHapPlayer, RendererGl)