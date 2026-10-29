package org.engine.minimal;

import android.content.Context;
import android.view.SurfaceHolder;
import org.libsdl.app.SDLSurface;

final class EngineSurface extends SDLSurface
{
    EngineSurface(Context context)
    {
        super(context);
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder)
    {
        // Surface 可在 Activity 仍存活时重建；此边界也必须等 bgfx 完全停下。
        EngineActivity.pauseRendering();
        super.surfaceDestroyed(holder);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height)
    {
        super.surfaceChanged(holder, format, width, height);
        EngineActivity.resumeRenderingIfReady();
    }
}
