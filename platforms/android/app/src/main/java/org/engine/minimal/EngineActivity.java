package org.engine.minimal;

import android.content.Context;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public final class EngineActivity extends SDLActivity
{
    private static native boolean nativePauseRendering();
    private static native void nativeResumeRendering();
    private static native boolean nativeRunV8Smoke(String icuDataFile);

    @Override protected String[] getLibraries()
    {
        // SDL3 和 C++ 运行库已静态链接到唯一的原生库。
        return new String[] {"main"};
    }

    private void runV8Smoke()
    {
        if (!BuildConfig.ENGINE_SCRIPT_V8)
        {
            Log.e("EngineV8", "APK V8 smoke unavailable: this package uses another script backend");
            return;
        }
        if (!BuildConfig.ENGINE_SCRIPT_ICU)
        {
            reportV8Smoke(nativeRunV8Smoke(""));
            return;
        }
        File directory = new File(getCacheDir(), "v8-smoke");
        if (!directory.isDirectory() && !directory.mkdirs())
        {
            Log.e("EngineV8", "APK V8 smoke FAIL: cannot create the private ICU directory");
            return;
        }
        File data = new File(directory, "icudtl.dat");
        // 每次测试重新复制，APK 升级后不会误用上一个 V8 版本的 ICU 数据。
        try (InputStream source = getAssets().open("v8/icudtl.dat");
             FileOutputStream destination = new FileOutputStream(data))
        {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = source.read(buffer)) != -1)
            {
                destination.write(buffer, 0, count);
            }
        }
        catch (IOException error)
        {
            Log.e("EngineV8", "APK V8 smoke FAIL: cannot copy ICU data", error);
            return;
        }
        reportV8Smoke(nativeRunV8Smoke(data.getAbsolutePath()));
    }

    private static void reportV8Smoke(boolean passed)
    {
        Log.println(passed ? Log.INFO : Log.ERROR, "EngineV8",
                    passed ? "APK V8 smoke PASS" : "APK V8 smoke FAIL");
    }

    @Override protected void main()
    {
        if (getIntent().getBooleanExtra("v8_smoke", false))
        {
            runV8Smoke();
            return;
        }
        super.main();
    }

    static void pauseRendering()
    {
        if (!mBrokenLibraries && !nativePauseRendering())
        {
            Log.e(
                "Engine",
                "Renderer did not acknowledge the Android pause request; terminating this process");
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

    @Override protected SDLSurface createSDLSurface(Context context)
    {
        return new EngineSurface(context);
    }

    @Override protected void onPause()
    {
        // 在 SDL/Android 开始销毁 Surface 前，等原生主线程关闭渲染器。
        pauseRendering();
        super.onPause();
    }

    @Override protected void onResume()
    {
        super.onResume();
        resumeRenderingIfReady();
    }

    @Override protected String[] getArguments()
    {
        // 普通启动不需要项目；仅为模拟器冒烟测试传递可选的帧数上限。
        int frames = getIntent().getIntExtra("frames", 0);
        if (frames > 0)
        {
            return new String[] {"--frames", Integer.toString(frames)};
        }
        return new String[0];
    }
}
