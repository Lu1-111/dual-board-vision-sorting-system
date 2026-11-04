# Cross builds: CAMERA_OPENCV_ROOT=/.../ffmpeg_opencv4.7.0_aarch64.
# Native tests: use pkg-config only for include/library search paths, link just core/imgproc.
CAMERA_OPENCV_ROOT = $$(CAMERA_OPENCV_ROOT)
OPENCV_INCLUDE_DIR = $$(OPENCV_INCLUDE_DIR)
OPENCV_LIB_DIR = $$(OPENCV_LIB_DIR)
!isEmpty(OPENCV_INCLUDE_DIR) {
    INCLUDEPATH += $$OPENCV_INCLUDE_DIR
    !isEmpty(OPENCV_LIB_DIR) {
        LIBS += -L$$OPENCV_LIB_DIR
        QMAKE_LFLAGS += -Wl,-rpath-link,$$OPENCV_LIB_DIR
    }
    LIBS += -lopencv_imgproc -lopencv_core
} else:!isEmpty(CAMERA_OPENCV_ROOT) {
    INCLUDEPATH += $$CAMERA_OPENCV_ROOT/include/opencv4
    LIBS += -L$$CAMERA_OPENCV_ROOT/lib -lopencv_imgproc -lopencv_core
    QMAKE_LFLAGS += -Wl,-rpath-link,$$CAMERA_OPENCV_ROOT/lib
} else {
    QMAKE_CXXFLAGS += $$system(pkg-config --cflags opencv4)
    LIBS += $$system(pkg-config --libs-only-L opencv4) -lopencv_imgproc -lopencv_core
}
