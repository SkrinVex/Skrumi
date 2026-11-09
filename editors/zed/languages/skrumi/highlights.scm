; Skrumi Tree-sitter Highlighting Queries for Zed (based on C grammar)

; Keywords
"if" @keyword
"else" @keyword
"while" @keyword
"for" @keyword
"return" @keyword
"break" @keyword
"continue" @keyword
"struct" @keyword
"const" @keyword
"enum" @keyword

((identifier) @keyword
  (#match? @keyword "^(try|catch|finally|throw|in)$"))

((identifier) @keyword.import
  (#match? @keyword.import "^(using|as)$"))

; Types
(primitive_type) @type.builtin
(type_identifier) @type

((type_identifier) @type.builtin
  (#match? @type.builtin "^(int|string|bool|void|array|map|func|float|double|char)$"))

((identifier) @type.builtin
  (#match? @type.builtin "^func$"))

; Boolean & Null Constants
((identifier) @boolean
  (#match? @boolean "^(true|false)$"))

((identifier) @constant.builtin
  (#match? @constant.builtin "^null$"))

((identifier) @variable.special
  (#match? @variable.special "^this$"))

(null) @constant.builtin

; Built-in Standard Library Functions
((identifier) @function.builtin
  (#match? @function.builtin "^(abs|absolute_path|access_log|acos|all_of|allow_cors|angle_difference|any_of|append_file|architecture|archive_extract|arg_or|args|array_all|array_any|array_chunk|array_copy|array_filter|array_find|array_flatten|array_index_of|array_map|array_reduce|array_reverse|array_shuffle|array_slice|array_sort|array_unique|asin|assert|assert_close|assert_equal|atan|atan2|beep|body|byte_size|cancel_window_close|canvas_png|ceil|char_length|circle_rect_collide|circles_collide|clamp|clamp01|clear|clear_window|client_ip|clip_begin|clip_end|clipboard_text|clock_ms|clock_ms64|close_tcp|close_window|color|connect_tcp|contains|cookie|copy|copy_array|copy_checked|copy_path|cos|create_canvas|cwd|debug|decode_image|degrees|delete|desktop_metrics|dns_lookup|double_clicked|download|draw_canvas|draw_circle|draw_circle_alpha|draw_circles|draw_ellipse|draw_font_text|draw_font_text_alpha|draw_frame|draw_gradient|draw_icon|draw_image|draw_image_alpha|draw_image_ex|draw_image_nine_slice|draw_image_part|draw_image_part_ex|draw_image_scaled|draw_line|draw_line_alpha|draw_lines|draw_particles|draw_polygon|draw_rect|draw_rect_alpha|draw_rects|draw_ring|draw_ring_alpha|draw_sprites|draw_text|draw_text_aligned|draw_text_alpha|draw_text_wrapped|draw_tiles|draw_to_canvas|draw_triangle|ends_with|env|env_default|env_get|env_required|env_set|env_user_compare_exchange|env_user_snapshot|error|exists|exit|exp|extract_archive|fade_out|fail|file_extension|file_name|file_sha256|file_size|file_stem|filter|find|find_first|floor|font_text_width|font_with_fallback|form|format_bytes|format_time|frame_delta|free_canvas|free_font|free_image|free_space|fs_copy|fs_exists|fs_exists_checked|fs_free_space|fs_home|fs_is_dir|fs_is_executable|fs_is_file|fs_is_link|fs_list|fs_list_checked|fs_lock|fs_make_dir|fs_mkdir_checked|fs_mkdtemp|fs_modified|fs_move|fs_owner|fs_permissions|fs_read_checked|fs_remove|fs_remove_all|fs_remove_checked|fs_rename_checked|fs_set_permissions|fs_sha256|fs_size|fs_temp_dir|fs_unlock|fs_write_atomic|gc_collect|get|get_or|getch|gfx_cancel_close|gfx_canvas_create|gfx_canvas_draw|gfx_canvas_free|gfx_canvas_png|gfx_canvas_select|gfx_circle|gfx_circle_alpha|gfx_circles|gfx_clear|gfx_clip_begin|gfx_clip_end|gfx_clipboard|gfx_close|gfx_close_requested|gfx_delta|gfx_desktop_metrics|gfx_double_click|gfx_down|gfx_ellipse|gfx_focused|gfx_font_fallback|gfx_font_free|gfx_font_load|gfx_font_text|gfx_font_text_alpha|gfx_font_width|gfx_frame|gfx_fullscreen|gfx_gradient|gfx_height|gfx_image_alpha|gfx_image_decode|gfx_image_draw|gfx_image_draw_part|gfx_image_ex|gfx_image_free|gfx_image_height|gfx_image_load|gfx_image_part_ex|gfx_image_pixel|gfx_image_width|gfx_intercept_close|gfx_line|gfx_line_alpha|gfx_lines|gfx_mouse_captured|gfx_mouse_delta|gfx_mouse_visible|gfx_mouse_x|gfx_mouse_y|gfx_open|gfx_poll|gfx_polygon|gfx_present|gfx_pressed|gfx_rect|gfx_rect_alpha|gfx_rects|gfx_repeat|gfx_resizable|gfx_resized|gfx_rgb|gfx_ring|gfx_ring_alpha|gfx_screenshot|gfx_set_clipboard|gfx_set_fullscreen|gfx_set_size|gfx_sprites|gfx_target_fps|gfx_text|gfx_text_alpha|gfx_text_input|gfx_text_missing|gfx_text_supported|gfx_text_width|gfx_tiles|gfx_ui_click|gfx_ui_drag|gfx_ui_drag_x|gfx_ui_drag_y|gfx_ui_focus|gfx_ui_focused|gfx_ui_hover|gfx_ui_layer_begin|gfx_ui_layer_end|gfx_ui_text|gfx_ui_text_font|gfx_ui_typing|gfx_ui_wheel|gfx_wheel|gfx_width|goto_xy|grapheme_length|grapheme_split|grapheme_substring|has|header|hide_cursor|home|home_dir|html_escape|http_delete|http_get|http_ok|http_patch|http_post|http_post_json|http_put|http_put_json|http_request|http_status|hypot|image_alpha|image_height|image_pixel|image_width|includes|index_of|info|input|insert|intercept_window_close|is_dir|is_even|is_executable|is_file|is_link|is_number|join|join_path|json_count|json_count_at|json_decode|json_escape|json_get|json_path|json_quote|json_safe|json_set|json_set_raw|json_type|json_type_at|json_valid|json_value|kbhit|key_down|key_pressed|key_repeat|keys|last_exit|length|lerp|list_dir|listen|listen_tls|load_font|load_font_file|load_image|log|log10|log_debug|log_error|log_info|log_warn|lower|make_dir|make_dirs_checked|make_temp_dir|max|max_body_size|method|min|modified_ms|mouse_delta|mouse_wheel|mouse_x|mouse_y|move_path|move_toward|not_found|now_text|open_path|open_window|os_arch|os_args|os_cwd|os_last_exit|os_open|os_platform|os_run|os_run_output|os_user_dir|owner|pad_left|pad_right|param|parent_dir|patch|path|path_absolute|path_extension|path_join|path_name|path_parent|path_safe_join|path_stem|pause_sound|permissions|platform|play_sound|play_sound_ex|play_tone|pop|post|pow|present_window|print|process_cancel|process_poll|process_release|process_result|process_start|push|put|query|query_param|radians|random|random_float|random_range|random_seed|range|read_checked|read_file|read_lines|read_resource|rects_collide|recv_tcp|redirect|registry_user_compare_exchange|registry_user_remove_empty_key|registry_user_snapshot|remove_at|remove_key|remove_path|remove_tree|render|repeat|replace|request_body|request_cookie|request_file_name|request_file_save|request_form|request_header|request_ip|request_method|request_param|request_path|request_query|request_query_param|reset_color|resize|resolve_host|resource_list|resource_names|resource_read|respond|respond_as|respond_html|respond_status|respond_text|resume_sound|reverse|rgb|round|route|run|run_capture|run_output|safe_join|save_json|save_path|save_screenshot|save_upload|secret|send_file|send_tcp|server_access_log|server_cors|server_download|server_header|server_listen|server_listen_tls|server_max_body|server_not_found|server_redirect|server_respond|server_route|server_send_file|server_set_cookie|server_static|server_stop|set|set_channel_volume|set_clipboard_text|set_cookie|set_env|set_header|set_master_volume|set_mouse_captured|set_mouse_visible|set_permissions|set_sound_pan|set_sound_volume|set_target_fps|set_window_fullscreen|set_window_resizable|set_window_size|sha256_bytes|show_cursor|shuffle|sign|sin|size|sleep_ms|slice|smoothstep|sort|sort_by|sound_channel_volume|sound_fade_out|sound_is_playing|sound_master_volume|sound_pan|sound_pause|sound_play|sound_play_ex|sound_playing|sound_stop|sound_stop_handle|sound_tone|sound_volume|spawn_particle|split|sqrt|starts_with|static_files|stop_server|stop_sound|stop_sounds|str_contains|str_ends_with|str_index_of|str_join|str_length|str_lower|str_repeat|str_replace|str_split|str_starts_with|str_substring|str_trim|str_upper|substring|sum|tan|tcp_close|tcp_connect|tcp_recv|tcp_send|temp_dir|template_render|term_clear|term_color|term_goto|term_hide_cursor|term_home|term_reset|term_show_cursor|term_write|text_ellipsis|text_height|text_input|text_missing_glyphs|text_supported|text_width|time_format|time_now_ms|to_float|to_int|to_string|transform|trim|type_of|ui_button|ui_checkbox|ui_checkbox_box|ui_click|ui_dialog|ui_dialog_box|ui_double_click|ui_drag|ui_drag_x|ui_drag_y|ui_draw_text|ui_focus|ui_focused|ui_hover|ui_layer_begin|ui_layer_end|ui_modal_begin|ui_modal_end|ui_primary_button|ui_progress_bar|ui_progress_busy|ui_progress_fraction|ui_scroll_begin|ui_scroll_end|ui_scroll_to|ui_scrollbar|ui_set_font|ui_text_field|ui_text_field_box|ui_text_height|ui_text_width|ui_theme|ui_typing|ui_unfocus|ui_wheel|ui_wrap_text|unix_time_ms|update_particles|upload_name|upper|uptime_ms|url_decode|url_encode|user_data_dir|user_directory|user_path_apply|user_path_plan|user_path_removal|user_path_validate|values|wait|warn|window_close_requested|window_focused|window_fullscreen|window_height|window_poll|window_resized|window_width|wrap|wrap_bitmap_text|write|write_atomic|write_file|write_lines)$"))

; Function Declarations & Calls
(function_declarator
  declarator: (identifier) @function)

(call_expression
  function: (identifier) @function.call)

; Parameters & Variables
(parameter_declaration
  declarator: (identifier) @variable.parameter)

(identifier) @variable

; Literals
(string_literal) @string
(system_lib_string) @string
(escape_sequence) @string.escape
(number_literal) @number
(char_literal) @string

; Comments
(comment) @comment

; Operators
"=" @operator
"+" @operator
"-" @operator
"*" @operator
"/" @operator
"%" @operator
"==" @operator
"!=" @operator
"<" @operator
"<=" @operator
">" @operator
">=" @operator
"&&" @operator
"||" @operator
"!" @operator
"+=" @operator
"-=" @operator
"*=" @operator
"/=" @operator

; Delimiters & Punctuation
";" @punctuation.delimiter
"," @punctuation.delimiter
"." @punctuation.delimiter

; Brackets
"{" @punctuation.bracket
"}" @punctuation.bracket
"[" @punctuation.bracket
"]" @punctuation.bracket
"(" @punctuation.bracket
")" @punctuation.bracket
