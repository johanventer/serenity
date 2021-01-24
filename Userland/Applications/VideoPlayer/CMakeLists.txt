set(SOURCES
    LibAV/MOVFile.cpp
    LibAV/RLEVideoDecoder.cpp
    VideoPlayerWidget.cpp
    VideoWidget.cpp
    main.cpp
)

serenity_app(VideoPlayer ICON app-video-player)
target_link_libraries(VideoPlayer LibAudio LibGUI LibGfx)
