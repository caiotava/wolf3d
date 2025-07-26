
find_package(SDL2 CONFIG REQUIRED)
find_package(PhysFS CONFIG REQUIRED)

add_executable(wolf3d
    filesystem.c
    ID_MM.c
    DETECT.c
    ID_CA.c
    ID_IN.c
    ID_PM.c
    ID_SD.c
    ID_US_1.c
    ID_VH.c
    ID_VL.c
    WL_ACT1.c
    WL_ACT2.c
    WL_AGENT.c
    WL_DEBUG.c
    WL_DRAW.c
    WL_GAME.c
    WL_INTER.c
    WL_MAIN.c
    WL_MENU.c
    WL_PLAY.c
    WL_SCALE.c
    WL_STATE.c
    WL_TEXT.c
    WOLFHACK.c
    signon.c
)

add_executable(test main.c filesystem.c)

target_link_libraries(wolf3d
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:PhysFS::PhysFS>,PhysFS::PhysFS,PhysFS::PhysFS-static>
)

target_link_libraries(test
        PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        $<IF:$<TARGET_EXISTS:PhysFS::PhysFS>,PhysFS::PhysFS,PhysFS::PhysFS-static>
)
