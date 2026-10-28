package org.engine.minimal;

import android.content.Context;
import android.util.Log;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public final class EngineActivity extends SDLActivity
{
    private static native boolean nativePauseRendering();
    private static native void nativeResumeRendering();

    static void pauseRendering()
    {
        if (!mBrokenLibraries && !nativePauseRendering())
        {
            Log.e("Engine", "Renderer did not acknowledge the Android pause request; terminating this process");
            android.os.Process.killProcess(android.os.Process.myPid());
            System.exit(1);
        }
    }

    static void resumeRenderingIfReady()
    {
        if (!mBrokenLibraries && mIsResumedCalled && mSurface != null && mSurface.mIsSurfaceReady)
        {
            nativeResumeRendering();
        }
    }

    @Override
    protected SDLSurface createSDLSurface(Context context)
    {
        return new EngineSurface(context);
    }

    @Override
    protected void onPause()
    {
        // 在 SDL/Android 开始销毁 Surface 前，等原生主线程关闭渲染器。
        pauseRendering();
        super.onPause();
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        resumeRenderingIfReady();
    }

    @Override
    protected String[] getArguments()
    {
        // 普通启动不需要项目；仅为模拟器冒烟测试传递可选的帧数上限。
        int frames = getIntent().getIntExtra("frames", 0);
        if (frames > 0)
        {
            return new String[] { "--frames", Integer.toString(frames) };
        }
        return new String[0];
    }
}
