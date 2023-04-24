#ifndef ERROR_HPP
#define ERROR_HPP

enum Error : int {
    SDLInitFailed               = -1,
    SDLWindowInitFailed         = -2,
    SDLVKSurfaceCreatedFailed   = -3,
    VKCreateFrameBufferFailed   = -4
};

#endif
