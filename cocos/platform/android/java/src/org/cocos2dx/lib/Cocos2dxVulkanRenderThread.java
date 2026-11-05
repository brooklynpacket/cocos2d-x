package org.cocos2dx.lib;

import android.os.Handler;
import android.os.Looper;
import android.view.Surface;

import java.util.ArrayList;

/**
 * Render thread used instead of GLSurfaceView's GL thread when the Vulkan
 * backend is active. It mirrors the GL thread's behaviour that the engine
 * relies on: queued events run on this thread even while paused, the engine
 * is initialised on the first surface, the renderer is told about every new
 * surface (onSurfaceChanged), and frames are drawn only while resumed with a
 * surface. Frame pacing comes from the swapchain (FIFO present).
 *
 * Unlike GLSurfaceView, the surface callbacks never wait for this thread:
 * the native side swaps the window under a short lock and only waits for
 * the GPU, so a long frame (e.g. a scene load) cannot block the UI thread.
 *
 * Like GLSurfaceView, the window is only reported as drawn
 * (surfaceRedrawNeededAsync) once a frame has been presented after the
 * request. Reporting it earlier lets the system remove the launch window
 * over an empty surface, which shows as the launch window shrinking into
 * black. The callbacks are released on a timeout or surface loss too, so a
 * slow first frame cannot hold the window forever.
 */
class Cocos2dxVulkanRenderThread extends Thread {
    private final Object mLock = new Object();
    private final ArrayList<Runnable> mEvents = new ArrayList<Runnable>();
    private final Cocos2dxRenderer mRenderer;

    private boolean mHasSurface;
    private boolean mResumed = true;
    private boolean mSurfaceNeedsChangedCallback;
    private boolean mInitialized;

    // finishDrawing callbacks from surfaceRedrawNeededAsync, each released
    // once the present count passes the value recorded with it.
    private static final long FINISH_DRAWING_TIMEOUT_MS = 4000;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final ArrayList<Runnable> mPendingFinishDrawing = new ArrayList<Runnable>();
    private final ArrayList<Long> mPendingFinishDrawingCounts = new ArrayList<Long>();

    Cocos2dxVulkanRenderThread(Cocos2dxRenderer renderer) {
        super("VulkanRenderThread");
        mRenderer = renderer;
    }

    void queueEvent(Runnable r) {
        if (r == null) return;
        synchronized (mLock) {
            mEvents.add(r);
            mLock.notifyAll();
        }
    }

    void setResumed(boolean resumed) {
        synchronized (mLock) {
            mResumed = resumed;
            mLock.notifyAll();
        }
    }

    // Called on the UI thread.
    void surfaceCreated(Surface surface) {
        Cocos2dxRenderer.nativeSetVulkanSurface(surface);
        synchronized (mLock) {
            mHasSurface = true;
            // GLSurfaceView calls onSurfaceChanged for every new EGL surface,
            // even when the size is unchanged; do the same.
            mSurfaceNeedsChangedCallback = true;
            mLock.notifyAll();
        }
    }

    // Called on the UI thread.
    void surfaceChanged(int width, int height) {
        Cocos2dxRenderer.nativeVulkanSurfaceChanged();
        synchronized (mLock) {
            mSurfaceNeedsChangedCallback = true;
            mLock.notifyAll();
        }
    }

    // Called on the UI thread. Returns once the native side no longer uses the surface.
    void surfaceDestroyed() {
        synchronized (mLock) {
            mHasSurface = false;
        }
        Cocos2dxRenderer.nativeSetVulkanSurface(null);
        releaseFinishDrawing(true);
    }

    // Called on the UI thread. finishDrawing runs on the UI thread after the
    // next present (or the timeout).
    void surfaceRedrawNeededAsync(final Runnable finishDrawing) {
        synchronized (mLock) {
            mPendingFinishDrawing.add(finishDrawing);
            mPendingFinishDrawingCounts.add(Cocos2dxRenderer.nativeGetVulkanPresentCount());
            mLock.notifyAll();
        }
        mMainHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                boolean pending;
                synchronized (mLock) {
                    int i = mPendingFinishDrawing.indexOf(finishDrawing);
                    pending = i >= 0;
                    if (pending) {
                        mPendingFinishDrawing.remove(i);
                        mPendingFinishDrawingCounts.remove(i);
                    }
                }
                if (pending) {
                    finishDrawing.run();
                }
            }
        }, FINISH_DRAWING_TIMEOUT_MS);
    }

    // Hands callbacks whose frame has been presented (or all of them) to the UI thread.
    private void releaseFinishDrawing(boolean all) {
        final ArrayList<Runnable> ready = new ArrayList<Runnable>();
        synchronized (mLock) {
            if (mPendingFinishDrawing.isEmpty()) return;
            long presented = all ? Long.MAX_VALUE : Cocos2dxRenderer.nativeGetVulkanPresentCount();
            for (int i = mPendingFinishDrawing.size() - 1; i >= 0; --i) {
                if (presented > mPendingFinishDrawingCounts.get(i)) {
                    ready.add(0, mPendingFinishDrawing.remove(i));
                    mPendingFinishDrawingCounts.remove(i);
                }
            }
        }
        if (ready.isEmpty()) return;
        Runnable run = new Runnable() {
            @Override
            public void run() {
                for (Runnable r : ready) r.run();
            }
        };
        if (Looper.myLooper() == Looper.getMainLooper()) {
            run.run();
        } else {
            mMainHandler.post(run);
        }
    }

    @Override
    public void run() {
        while (true) {
            Runnable event = null;
            boolean init = false;
            boolean changed = false;
            synchronized (mLock) {
                while (true) {
                    if (!mEvents.isEmpty()) {
                        event = mEvents.remove(0);
                        break;
                    }
                    if (mHasSurface && mResumed) {
                        if (!mInitialized) {
                            mInitialized = true;
                            init = true;
                        }
                        if (mSurfaceNeedsChangedCallback) {
                            mSurfaceNeedsChangedCallback = false;
                            changed = true;
                        }
                        break;
                    }
                    try {
                        mLock.wait();
                    } catch (InterruptedException e) {
                        return;
                    }
                }
            }

            if (event != null) {
                event.run();
                continue;
            }
            if (init) {
                mRenderer.onVulkanSurfaceCreated();
            }
            if (changed) {
                mRenderer.onVulkanSurfaceChanged();
            }
            mRenderer.onDrawFrame(null);
            releaseFinishDrawing(false);
        }
    }
}
