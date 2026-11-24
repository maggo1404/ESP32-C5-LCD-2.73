# ITS-G5 decoder component: pure C99, no ESP-IDF APIs, host-testable (see test/).
idf_component_register(
    SRCS
        "src/uper.c"
        "src/its_frame.c"
        "src/its_cam.c"
        "src/its_denm.c"
        "src/its_mapspat.c"
        "src/its_names.c"
        "src/its_store.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
)
