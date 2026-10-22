# Asset tools (offline helpers, the game itself reads the original files).
add_executable( otdu2_bnk_tool bnk_tool/main.cpp )
target_link_libraries( otdu2_bnk_tool PRIVATE otdu_engine )

add_executable( otdu2_sob_tool sob_tool/main.cpp )
target_link_libraries( otdu2_sob_tool PRIVATE otdu_engine )
