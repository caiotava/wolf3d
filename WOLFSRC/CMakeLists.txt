
find_package(SDL2 CONFIG REQUIRED)
find_package(PhysFS CONFIG REQUIRED)

add_executable(wolf3d
    filesystem.c
    CONTIGSC.C
    DETECT.C
    ID_CA.c
    ID_IN.C
    ID_MM.C
    ID_PM.C
    ID_SD.C
    ID_US_1.C
    ID_VH.C
    ID_VL.C
    MUNGE.C
    WL_ACT1.C
    WL_ACT2.C
    WL_AGENT.C
    WL_DEBUG.C
    WL_DRAW.C
    WL_GAME.C
    WL_INTER.C
    WL_MAIN.C
    WL_MENU.C
    WL_PLAY.C
    WL_SCALE.C
    WL_STATE.C
    WL_TEXT.C
    WOLFHACK.C
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