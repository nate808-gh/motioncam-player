// FILE: src/Gui/GuiRender.cpp
#define IMGUI_DEFINE_MATH_OPERATORS

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "Gui/GuiOverlay.h"
#include "Gui/GuiStyles.h"
#include "Gui/GuiUtils.h"
#include "Utils/IconsMaterial.h"
#include "Utils/DebugLog.h" // For LogToFile, if logging is desired

#include "App/App.h"
#include "Playback/PlaybackController.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"


#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif

namespace fs = std::filesystem;

namespace GuiOverlay {

    bool show_playlist_aux = false;

    void beginFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    UIData gatherData(App* appInstance) {
        UIData data = {};
        if (!appInstance) return data;

        PlaybackController* playbackController = appInstance->m_playbackController_ptr;
        DecoderWrapper* decoderWrapper = appInstance->m_decoderWrapper_ptr;


        if (playbackController) {
            data.isPaused = playbackController->isPaused();
            data.isZoomedToNative = playbackController->isZoomNativePixels();
            data.currentFrameIndex = playbackController->getCurrentFrameIndex();
        }
        else {
            data.isPaused = true;
            data.isZoomedToNative = false;
            data.currentFrameIndex = 0;
        }
        data.actualDisplayFps = PlaybackController::getDisplayFps();


        if (appInstance->m_currentFileIndex >= 0 && static_cast<size_t>(appInstance->m_currentFileIndex) < appInstance->m_fileList.size()) {
            try { data.currentFileName = fs::path(appInstance->m_fileList[appInstance->m_currentFileIndex]).filename().string(); }
            catch (const std::exception&) { data.currentFileName = "Error"; }
        }
        else { data.currentFileName = "N/A"; }

        if (decoderWrapper && decoderWrapper->getDecoder()) {
            const auto& frames = decoderWrapper->getDecoder()->getFrames();
            data.totalFramesInFile = frames.size();
            int64_t firstFrameTsOfSegment = 0;
            if (playbackController) {
                if (auto optTs = playbackController->getFirstFrameMediaTimestampOfSegment()) {
                    firstFrameTsOfSegment = *optTs;
                }
                else if (!frames.empty()) {
                    firstFrameTsOfSegment = frames.front();
                }
            }
            else if (!frames.empty()) {
                firstFrameTsOfSegment = frames.front();
            }


            if (data.totalFramesInFile > 0 && data.currentFrameIndex < data.totalFramesInFile) {
                if (data.currentFrameIndex < frames.size()) {
                    int64_t current_ts_in_file = frames[data.currentFrameIndex];
                    data.currentVideoTimeSec = static_cast<double>(current_ts_in_file - firstFrameTsOfSegment) * 1e-9;
                    if (data.currentVideoTimeSec < 0.0) data.currentVideoTimeSec = 0.0;
                    data.videoTimestampStr = GuiUtils::formatHMS(current_ts_in_file - firstFrameTsOfSegment);
                }
                else {
                    data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
                }
            }
            else {
                data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
            }
            if (data.totalFramesInFile >= 2) {
                if (!frames.empty()) {
                    data.totalDurationSec = static_cast<double>(frames.back() - firstFrameTsOfSegment) * 1e-9;
                    if (data.totalDurationSec < 0.0) data.totalDurationSec = 0.0;
                    if (data.totalDurationSec > 1e-6 && data.totalFramesInFile > 1) {
                        data.capturedFps = (static_cast<double>(data.totalFramesInFile - 1) / data.totalDurationSec);
                    }
                    else { data.capturedFps = 0.0; }
                }
                else {
                    data.totalDurationSec = 0.0; data.capturedFps = 0.0;
                }
            }
            else { data.totalDurationSec = 0.0; data.capturedFps = 0.0; }
        }
        else {
            data.totalFramesInFile = 0;
            data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
            data.totalDurationSec = 0.0; data.capturedFps = 0.0;
        }

        if (appInstance->m_audio) {
            int64_t audioAnchorMediaTs = appInstance->m_audio->getAudioAnchorTimestampNs();
            int64_t lastQueuedAudioOffsetFromAnchor = appInstance->m_audio->getLastQueuedTimestamp();
            int64_t actualLastQueuedAudioMediaTs = audioAnchorMediaTs + lastQueuedAudioOffsetFromAnchor;
            data.audioTimestampStr = GuiUtils::formatHMS(actualLastQueuedAudioMediaTs - (playbackController ? playbackController->getFirstFrameMediaTimestampOfSegment().value_or(0) : 0));
        }
        else {
            data.audioTimestampStr = GuiUtils::formatHMS(0);
        }

        if (playbackController && decoderWrapper && decoderWrapper->getDecoder() && appInstance->m_audio) {
            const auto& frames = decoderWrapper->getDecoder()->getFrames();
            if (data.currentFrameIndex < frames.size()) {
                int64_t currentActualVideoMediaTs = frames[data.currentFrameIndex];
                int64_t audioAnchorMediaTs = appInstance->m_audio->getAudioAnchorTimestampNs();
                int64_t lastQueuedAudioOffsetFromAnchor = appInstance->m_audio->getLastQueuedTimestamp();
                int64_t actualLastQueuedAudioMediaTs = audioAnchorMediaTs + lastQueuedAudioOffsetFromAnchor;

                double deltaAvSyncSec = static_cast<double>(actualLastQueuedAudioMediaTs - currentActualVideoMediaTs) * 1e-9;
                std::ostringstream oss; oss << std::showpos << std::fixed << std::setprecision(3) << deltaAvSyncSec << "s";
                data.avSyncDeltaStr = oss.str();
            }
            else { data.avSyncDeltaStr = "N/A (idx err)"; }
        }
        else { data.avSyncDeltaStr = "N/A"; }

        data.cfaOverride = appInstance->m_cfaOverride;
        data.cfaFromMetadataStr = appInstance->m_cfaStringFromMetadata;
        data.isFullscreen = appInstance->m_isFullscreen;
        data.showMetrics = appInstance->m_showMetrics;
        data.showHelpPage = appInstance->m_showHelpPage;

        data.decodedWidth = appInstance->m_decodedWidth;
        data.decodedHeight = appInstance->m_decodedHeight;

        data.totalLoopTimeMs = appInstance->m_totalLoopTimeMs;
        data.gpuWaitTimeMs = appInstance->m_gpuWaitTimeMs;
        data.decodeTimeMs = appInstance->m_decodeTimeMs;
        data.renderPrepTimeMs = appInstance->m_renderPrepTimeMs;
        data.guiRenderTimeMs = appInstance->m_guiRenderTimeMs;
        data.vkSubmitPresentTimeMs = appInstance->m_vkSubmitPresentTimeMs;
        data.appLogicTimeMs = appInstance->m_appLogicTimeMs;
        data.sleepTimeMs = appInstance->m_sleepTimeMs;

        return data;
    }


    void render(App* appInstance) {
        if (!appInstance) return;
        UIData ui = GuiOverlay::gatherData(appInstance);
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !io.WantCaptureMouse) {
            ImGui::OpenPopup("AppContextMenu");
        }

        if (ImGui::BeginPopup("AppContextMenu")) {
            bool canOperateOnCurrentFile = appInstance && appInstance->m_currentFileIndex != -1 &&
                static_cast<size_t>(appInstance->m_currentFileIndex) < appInstance->m_fileList.size();
            bool playlistNotEmpty = appInstance && !appInstance->m_fileList.empty();

            if (ImGui::MenuItem("Save Current Frame as DNG", nullptr, false, canOperateOnCurrentFile)) {
                if (appInstance) appInstance->saveCurrentFrameAsDng();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Soft Delete MCRAW", nullptr, false, canOperateOnCurrentFile)) {
                if (appInstance) appInstance->softDeleteCurrentFile();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Send Current to motioncam-fs", nullptr, false, canOperateOnCurrentFile)) {
                if (appInstance) appInstance->sendCurrentFileToMotionCamFS();
            }
            if (ImGui::MenuItem("Send All in Playlist to motioncam-fs", nullptr, false, playlistNotEmpty)) {
                if (appInstance) appInstance->sendAllPlaylistFilesToMotionCamFS();
            }
            ImGui::EndPopup();
        }

        float current_playlist_window_width = 0.0f;
        bool playlist_window_is_visible = false;

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        if (GuiOverlay::show_playlist_aux) {
            const float initial_playlist_width = 320.0f;
            float default_playlist_height = viewport->WorkSize.y * 0.80f;
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - initial_playlist_width - style.WindowPadding.x, viewport->WorkPos.y + style.WindowPadding.y), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(initial_playlist_width, default_playlist_height), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(initial_playlist_width * 0.5f, 100.0f), ImVec2(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y - 2 * style.WindowPadding.y));

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.09f, 0.95f));

            if (ImGui::Begin("PLAYLIST_AUX_TOGGLED", &GuiOverlay::show_playlist_aux, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
                current_playlist_window_width = ImGui::GetWindowSize().x;
                playlist_window_is_visible = true;
                if (appInstance->m_fileList.empty()) {
                    ImGui::TextDisabled(" (empty)");
                }
                else {
                    for (int i = 0; i < static_cast<int>(appInstance->m_fileList.size()); ++i) {
                        const std::string& filePath = appInstance->m_fileList[i];
                        std::string filename_to_display = fs::path(filePath).stem().string();
                        bool is_selected = (appInstance->m_currentFileIndex == i);
                        char entry_buf[512];
                        snprintf(entry_buf, sizeof(entry_buf), "%2d. %s ", i + 1, filename_to_display.c_str());
                        if (is_selected) ImGui::PushStyleColor(ImGuiCol_Header, style.Colors[ImGuiCol_HeaderActive]);
                        if (ImGui::Selectable(entry_buf, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            if (!is_selected) {
                                bool originalFirstFileLoadedState = appInstance->m_firstFileLoaded;
                                appInstance->m_firstFileLoaded = true;
                                appInstance->loadFileAtIndex(i);
                                appInstance->m_firstFileLoaded = originalFirstFileLoadedState;
                            }
                        }
                        if (is_selected) ImGui::PopStyleColor();
                    }
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
        (void)current_playlist_window_width;
        (void)playlist_window_is_visible;


        if (ui.showHelpPage) {
            ImGui::SetNextWindowSize(ImVec2(450, 420), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x / 2 - 225, viewport->WorkPos.y + viewport->WorkSize.y / 2 - 210), ImGuiCond_Appearing);

            bool help_open_flag = ui.showHelpPage;
            if (ImGui::Begin("Help - Keyboard Shortcuts", &help_open_flag, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::Text("Playback Controls:");
                ImGui::BulletText("[Space]        : Play / Pause");
                ImGui::BulletText("[Left Arrow]   : Previous Frame (Step Back)");
                ImGui::BulletText("[Right Arrow]  : Next Frame (Step Forward)");
                ImGui::BulletText("[Home]         : Go to First Frame");
                ImGui::BulletText("[End]          : Go to Last Frame");
                ImGui::Separator();
                ImGui::Text("File Navigation:");
                ImGui::BulletText("[[ (L-Bracket)]: Previous File in Playlist");
                ImGui::BulletText("[] (R-Bracket)]: Next File in Playlist");
                ImGui::BulletText("[Ctrl + O]     : Open File Dialog");
                ImGui::Separator();
                ImGui::Text("Display & UI:");
                ImGui::BulletText("[F] or [F11]   : Toggle Fullscreen");
                ImGui::BulletText("[Z]            : Toggle Zoom (Native Pixels / Fit to Window)");
                ImGui::BulletText("[M]            : Toggle Metrics Overlay");
                ImGui::BulletText("[H] or [F1]    : Toggle This Help Page");
                ImGui::BulletText("[Tab]          : Toggle Main UI Controls");
                ImGui::BulletText("[Esc]          : Exit Fullscreen / Close Popups / Quit");
                ImGui::Separator();
                ImGui::Text("Application:");
                ImGui::BulletText("[Ctrl + Q]     : Quit Application");
            }
            ImGui::End();
            if (!help_open_flag && ui.showHelpPage) {
                appInstance->toggleHelpPage();
            }
        }

        ImVec2 sizeLargeButton, sizeSmallButton, sizeAuxOverlayButton, sizePlayPauseButton;
        float largeButtonFrameHeight, smallButtonFrameHeight, auxOverlayButtonFrameHeight, playPauseButtonFrameHeight;

        if (GuiStyles::G_LargeIconFont) ImGui::PushFont(GuiStyles::G_LargeIconFont);
        float large_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
        float large_button_dim = (large_icon_line_height_sp + GuiStyles::G_LARGE_BUTTON_PADDING.y * 2.0f) * 1.1f;
        sizeLargeButton = ImVec2(large_button_dim, large_button_dim);
        largeButtonFrameHeight = sizeLargeButton.y;
        sizePlayPauseButton = ImVec2(large_button_dim * 1.30f, large_button_dim * 1.30f);
        playPauseButtonFrameHeight = sizePlayPauseButton.y;
        if (GuiStyles::G_LargeIconFont) ImGui::PopFont();

        if (GuiStyles::G_SmallIconFont) ImGui::PushFont(GuiStyles::G_SmallIconFont);
        float small_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
        sizeSmallButton = ImVec2(small_icon_line_height_sp + GuiStyles::G_SMALL_BUTTON_PADDING.x * 2.0f + 4.0f, small_icon_line_height_sp + GuiStyles::G_SMALL_BUTTON_PADDING.y * 2.0f + 2.0f);
        smallButtonFrameHeight = sizeSmallButton.y;
        if (GuiStyles::G_SmallIconFont) ImGui::PopFont();

        if (GuiStyles::G_AuxOverlayIconFont) ImGui::PushFont(GuiStyles::G_AuxOverlayIconFont);
        float aux_overlay_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
        sizeAuxOverlayButton = ImVec2(aux_overlay_icon_line_height_sp + GuiStyles::G_AUX_OVERLAY_BUTTON_PADDING.x * 2.0f + 3.0f, aux_overlay_icon_line_height_sp + GuiStyles::G_AUX_OVERLAY_BUTTON_PADDING.y * 2.0f + 1.5f);
        auxOverlayButtonFrameHeight = sizeAuxOverlayButton.y;
        if (GuiStyles::G_AuxOverlayIconFont) ImGui::PopFont();

        const float general_inter_button_gap = style.ItemSpacing.x * 1.0f;
        const float tight_inter_button_gap = style.ItemSpacing.x * 0.3f;
        float main_panel_buttons_total_width = sizeLargeButton.x + tight_inter_button_gap + sizeSmallButton.x + general_inter_button_gap + sizePlayPauseButton.x + general_inter_button_gap + sizeSmallButton.x + tight_inter_button_gap + sizeLargeButton.x;

        std::string currentTimeStr = GuiUtils::format_mm_ss(ui.currentVideoTimeSec);
        std::string totalTimeStr = (ui.totalDurationSec > 0) ? GuiUtils::format_mm_ss(ui.totalDurationSec) : "00:00";
        float current_time_text_width_calc = ImGui::CalcTextSize(currentTimeStr.c_str()).x;
        float total_time_text_width_calc = ImGui::CalcTextSize(totalTimeStr.c_str()).x;
        const float min_scrubber_width_pref = 150.0f;
        float time_row_min_width_calc = current_time_text_width_calc + style.ItemSpacing.x + min_scrubber_width_pref + style.ItemSpacing.x + total_time_text_width_calc;
        float time_row_text_height_calc = ImGui::CalcTextSize("00:00").y;

        float aux_button_effective_item_spacing_x = style.ItemSpacing.x * 0.25f;
        float aux_vertical_spacing_tight = style.ItemSpacing.y * 0.25f;
        float aux_buttons_grid_width = (sizeAuxOverlayButton.x * 2.0f) + aux_button_effective_item_spacing_x;
        float aux_buttons_grid_height = (sizeAuxOverlayButton.y * 2.0f) + (aux_vertical_spacing_tight * 1.0f);

        float base_desired_panel_content_width = std::max(main_panel_buttons_total_width, time_row_min_width_calc);
        float min_content_for_main_and_aux_grid = main_panel_buttons_total_width + aux_buttons_grid_width + style.ItemSpacing.x * 2.0f;
        base_desired_panel_content_width = std::max(base_desired_panel_content_width, min_content_for_main_and_aux_grid);
        base_desired_panel_content_width = std::max(base_desired_panel_content_width, 380.0f);

        float final_desired_panel_content_width = base_desired_panel_content_width * 1.15f;
        const float actual_panel_total_width = final_desired_panel_content_width + 2.0f * GuiStyles::PANEL_HORIZONTAL_PADDING;
        float actual_main_button_row_max_height = playPauseButtonFrameHeight;
        float main_panel_estimated_content_height = time_row_text_height_calc + style.ItemSpacing.y * 0.5f + std::max(actual_main_button_row_max_height, aux_buttons_grid_height);
        float main_panel_height_for_positioning = main_panel_estimated_content_height + 2.0f * GuiStyles::PANEL_VERTICAL_PADDING;

        float main_panel_center_x_coord = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
        float main_panel_pos_x_top_left = main_panel_center_x_coord - actual_panel_total_width / 2.0f;
        main_panel_pos_x_top_left = std::max(main_panel_pos_x_top_left, viewport->WorkPos.x + style.WindowPadding.x);

        float bottom_margin_percentage = 0.12f;
        float main_panel_pos_y_top_left = viewport->WorkPos.y + viewport->WorkSize.y * (1.0f - bottom_margin_percentage) - main_panel_height_for_positioning;
        main_panel_pos_y_top_left = std::max(main_panel_pos_y_top_left, viewport->WorkPos.y + style.WindowPadding.y);

        ImGui::SetNextWindowPos(ImVec2(main_panel_pos_x_top_left, main_panel_pos_y_top_left), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(actual_panel_total_width, 0), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.10f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.14f, 0.15f, 0.16f, 0.70f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, GuiStyles::PILL_RADIUS);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(GuiStyles::PANEL_HORIZONTAL_PADDING, GuiStyles::PANEL_VERTICAL_PADDING));
        ImGuiWindowFlags control_panel_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize;

        float y_base_for_button_row_content_area = 0.0f;

        if (ImGui::Begin("ControlsPanel", nullptr, control_panel_flags)) {
            const float panel_content_width_for_layout = ImGui::GetContentRegionAvail().x;
            const float thinner_slider_padding_y = 1.0f;

            float current_time_width = current_time_text_width_calc;
            float total_time_width = total_time_text_width_calc;
            float scrubber_width = panel_content_width_for_layout - current_time_width - total_time_width - 2 * style.ItemSpacing.x;
            scrubber_width = std::max(scrubber_width, 10.0f);
            float full_time_row_width = current_time_width + style.ItemSpacing.x + scrubber_width + style.ItemSpacing.x + total_time_width;
            float center_x_offset_time_row = (panel_content_width_for_layout - full_time_row_width) / 2.0f;

            float initial_cursor_y_for_time_row = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x_offset_time_row);
            ImGui::SetCursorPosY(initial_cursor_y_for_time_row);
            ImGui::TextUnformatted(currentTimeStr.c_str());
            ImGui::SameLine(0.0f, style.ItemSpacing.x);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, thinner_slider_padding_y));
            float slider_height_with_padding = ImGui::GetFrameHeight();
            ImGui::PopStyleVar();
            float slider_y_offset = (time_row_text_height_calc - slider_height_with_padding) / 2.0f;

            ImGui::SetCursorPosY(initial_cursor_y_for_time_row + slider_y_offset);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, thinner_slider_padding_y));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.14f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.02f, 0.56f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.02f, 0.56f, 0.98f, 1.0f));
            ImGui::PushItemWidth(scrubber_width);

            if (ui.totalFramesInFile > 0 && appInstance->m_playbackController_ptr) {
                int current_frame_idx_slider = static_cast<int>(ui.currentFrameIndex);
                int total_frames_slider = std::max(0, static_cast<int>(ui.totalFramesInFile) - 1);

                bool value_changed_by_user_drag = ImGui::SliderInt("##Scrubber", &current_frame_idx_slider, 0, total_frames_slider, "", ImGuiSliderFlags_AlwaysClamp);

                static bool was_paused_state_before_scrub = false;
                static bool scrub_in_progress = false;

                if (ImGui::IsItemActivated()) {
                    LogToFile("[GuiRender::Slider] Scrub ACTIVATED.");
                    scrub_in_progress = true;
                    was_paused_state_before_scrub = appInstance->m_playbackController_ptr->isPaused();
                    if (!was_paused_state_before_scrub) {
                        LogToFile("[GuiRender::Slider] Was playing, pausing for scrub via handleKey(SPACE).");
                        appInstance->handleKey(GLFW_KEY_SPACE, 0); // This will toggle pause and call recordPauseTime
                    }
                }

                if (ImGui::IsItemActive() && value_changed_by_user_drag) {
                    LogToFile(std::string("[GuiRender::Slider] Scrub DRAG, slider val: ") + std::to_string(current_frame_idx_slider) + ". Calling performSeek.");
                    appInstance->performSeek(static_cast<size_t>(current_frame_idx_slider));
                }

                if (scrub_in_progress && ImGui::IsItemDeactivated()) {
                    LogToFile(std::string("[GuiRender::Slider] Scrub DEACTIVATED. Final slider val: ") + std::to_string(current_frame_idx_slider) +
                        ", Current PB idx (after last drag seek, if any): " + std::to_string(appInstance->m_playbackController_ptr->getCurrentFrameIndex()));
                    scrub_in_progress = false;

                    if (static_cast<size_t>(current_frame_idx_slider) != appInstance->m_playbackController_ptr->getCurrentFrameIndex()) {
                        LogToFile(std::string("[GuiRender::Slider] Scrub DEACTIVATED, value different from PB. Final seek to: ") + std::to_string(current_frame_idx_slider));
                        appInstance->performSeek(static_cast<size_t>(current_frame_idx_slider));
                    }

                    if (!was_paused_state_before_scrub) {
                        LogToFile("[GuiRender::Slider] Scrub ended, was playing before. Resuming playback via handleKey(SPACE).");
                        appInstance->handleKey(GLFW_KEY_SPACE, 0); // This will unpause and call anchorPlaybackTimeForResume
                    }
                    else {
                        LogToFile("[GuiRender::Slider] Scrub ended, was paused. Stays paused. Anchor already set by (final) performSeek for paused state.");
                    }
                    was_paused_state_before_scrub = false;
                }
            }
            else {
                int dummy = 0;
                ImGui::SliderInt("##Timeline", &dummy, 0, 0, "", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(4);
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            ImGui::SetCursorPosY(initial_cursor_y_for_time_row);
            ImGui::TextUnformatted(totalTimeStr.c_str());

            ImGui::Dummy(ImVec2(0, style.ItemSpacing.y * 0.5f));
            y_base_for_button_row_content_area = ImGui::GetCursorPosY();

            float center_x_offset_button_row = (panel_content_width_for_layout - main_panel_buttons_total_width) / 2.0f;
            float current_x_for_buttons = ImGui::GetCursorPosX() + center_x_offset_button_row;

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

            float y_for_play_pause_button = y_base_for_button_row_content_area;
            float y_for_skip_buttons = y_base_for_button_row_content_area + (playPauseButtonFrameHeight - largeButtonFrameHeight) / 2.0f;
            float y_for_small_buttons = y_base_for_button_row_content_area + (playPauseButtonFrameHeight - smallButtonFrameHeight) / 2.0f;

            ImGui::PushFont(GuiStyles::G_LargeIconFont);
            ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_skip_buttons));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_LARGE_BUTTON_PADDING);
            if (ImGui::Button(ICON_MD_SKIP_PREVIOUS, sizeLargeButton)) appInstance->handleKey(GLFW_KEY_LEFT_BRACKET, 0);
            ImGui::PopStyleVar();
            current_x_for_buttons += sizeLargeButton.x + tight_inter_button_gap;
            ImGui::PopFont();

            ImGui::PushFont(GuiStyles::G_SmallIconFont);
            ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_small_buttons));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_SMALL_BUTTON_PADDING);
            if (ImGui::Button(ICON_MD_KEYBOARD_ARROW_LEFT, sizeSmallButton)) appInstance->handleKey(GLFW_KEY_LEFT, 0);
            ImGui::PopStyleVar();
            current_x_for_buttons += sizeSmallButton.x + general_inter_button_gap;
            ImGui::PopFont();

            ImGui::PushFont(GuiStyles::G_LargeIconFont);
            ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_play_pause_button));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sizePlayPauseButton.x * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.01f, 0.45f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.02f, 0.55f, 1.00f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.01f, 0.35f, 0.70f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_LARGE_BUTTON_PADDING);
            if (ImGui::Button(ui.isPaused ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE, sizePlayPauseButton)) appInstance->handleKey(GLFW_KEY_SPACE, 0);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            current_x_for_buttons += sizePlayPauseButton.x + general_inter_button_gap;
            ImGui::PopFont();

            ImGui::PushFont(GuiStyles::G_SmallIconFont);
            ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_small_buttons));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_SMALL_BUTTON_PADDING);
            if (ImGui::Button(ICON_MD_KEYBOARD_ARROW_RIGHT, sizeSmallButton)) appInstance->handleKey(GLFW_KEY_RIGHT, 0);
            ImGui::PopStyleVar();
            current_x_for_buttons += sizeSmallButton.x + tight_inter_button_gap;
            ImGui::PopFont();

            ImGui::PushFont(GuiStyles::G_LargeIconFont);
            ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_skip_buttons));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_LARGE_BUTTON_PADDING);
            if (ImGui::Button(ICON_MD_SKIP_NEXT, sizeLargeButton)) appInstance->handleKey(GLFW_KEY_RIGHT_BRACKET, 0);
            ImGui::PopStyleVar();
            ImGui::PopFont();

            ImGui::PopStyleColor();

            if (GuiStyles::G_AuxOverlayIconFont && y_base_for_button_row_content_area > 0.0f) {
                ImVec2 mainPanelWindowPos = ImGui::GetWindowPos();
                ImVec2 contentRegionTopLeftScreenPos = mainPanelWindowPos + ImVec2(GuiStyles::PANEL_HORIZONTAL_PADDING, GuiStyles::PANEL_VERTICAL_PADDING);
                float main_playback_buttons_row_visual_center_y_offset_in_content = y_base_for_button_row_content_area + playPauseButtonFrameHeight / 2.0f;
                float screen_y_for_aux_grid_center = contentRegionTopLeftScreenPos.y + main_playback_buttons_row_visual_center_y_offset_in_content;

                float screen_y_for_aux_grid_top_row = screen_y_for_aux_grid_center - aux_buttons_grid_height / 2.0f;
                float screen_x_of_content_right_edge = contentRegionTopLeftScreenPos.x + panel_content_width_for_layout;
                float screen_x_for_aux_grid_left_col = screen_x_of_content_right_edge - aux_buttons_grid_width;

                ImGui::PushFont(GuiStyles::G_AuxOverlayIconFont);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.75f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiStyles::G_AUX_OVERLAY_BUTTON_PADDING);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(aux_button_effective_item_spacing_x, aux_vertical_spacing_tight));

                ImGui::SetCursorScreenPos(ImVec2(screen_x_for_aux_grid_left_col, screen_y_for_aux_grid_top_row));
                bool muted = appInstance->m_audio && appInstance->m_audio->isEffectivelyMuted();
                if (ImGui::Button(muted ? ICON_MD_VOLUME_OFF : ICON_MD_VOLUME_UP, sizeAuxOverlayButton)) {
                    if (appInstance->m_audio) appInstance->m_audio->setForceMute(!muted);
                }
                ImGui::SameLine(0.0f, aux_button_effective_item_spacing_x);
                if (ImGui::Button(ICON_MD_INSIGHTS, sizeAuxOverlayButton)) {
                    appInstance->m_showMetrics = !appInstance->m_showMetrics;
                }

                float screen_y_for_aux_grid_middle_row = screen_y_for_aux_grid_top_row + sizeAuxOverlayButton.y + aux_vertical_spacing_tight;
                ImGui::SetCursorScreenPos(ImVec2(screen_x_for_aux_grid_left_col, screen_y_for_aux_grid_middle_row));
                if (ImGui::Button(ICON_MD_HELP_OUTLINE, sizeAuxOverlayButton)) {
                    appInstance->toggleHelpPage();
                }
                ImGui::SameLine(0.0f, aux_button_effective_item_spacing_x);
                if (ImGui::Button(ICON_MD_MENU, sizeAuxOverlayButton)) {
                    GuiOverlay::show_playlist_aux = !GuiOverlay::show_playlist_aux;
                }

                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::End();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        if (ui.showMetrics) {
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + style.WindowPadding.x, viewport->WorkPos.y + style.WindowPadding.y), ImGuiCond_Appearing);
            ImGui::SetNextWindowBgAlpha(0.75f);
            ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));
            ImGuiWindowFlags metrics_window_flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

            if (ImGui::Begin("METRICS", &appInstance->m_showMetrics, metrics_window_flags)) {
                ImGui::Text("File: %s", ui.currentFileName.c_str());
                ImGui::Text("Frame: %zu / %zu", ui.currentFrameIndex + (ui.totalFramesInFile > 0 ? 1 : 0), ui.totalFramesInFile);
                ImGui::Text("Time: %s / %s", ui.videoTimestampStr.c_str(), GuiUtils::formatHMS(static_cast<int64_t>(ui.totalDurationSec * 1e9)).c_str());
                ImGui::Text("Decoded Res: %d x %d", ui.decodedWidth, ui.decodedHeight);
                ImGui::Separator();
                ImGui::Text("Captured FPS: %.2f", ui.capturedFps);
                ImGui::Text("Display FPS: %.1f", ui.actualDisplayFps);
                ImGui::Text("Audio TS: %s", ui.audioTimestampStr.c_str());
                ImGui::Text("A/V Sync: %s", ui.avSyncDeltaStr.c_str());
                ImGui::Separator();

                ImGui::Text("Loop Times (ms): Total: %.1f", ui.totalLoopTimeMs);
                ImGui::Text("  GPU Wait: %.1f, Decode: %.1f", ui.gpuWaitTimeMs, ui.decodeTimeMs);
                ImGui::Text("  RenderPrep: %.1f, GUI: %.1f", ui.renderPrepTimeMs, ui.guiRenderTimeMs);
                ImGui::Text("  VK Submit/Present: %.1f", ui.vkSubmitPresentTimeMs);
                ImGui::Text("  App Logic (Events/PB/Audio): %.1f", ui.appLogicTimeMs);
                ImGui::Text("  Sleep: %.1f", ui.sleepTimeMs);

                ImGui::Separator();
                ImGui::Text("CFA: %s (Meta: %s)", ui.cfaOverride.has_value() ? std::to_string(ui.cfaOverride.value()).c_str() : "Auto", ui.cfaFromMetadataStr.c_str());
                ImGui::Text("Mode: %s, Zoom: %s", ui.isFullscreen ? "Fullscreen" : "Windowed", ui.isZoomedToNative ? "Native Pixels" : "Fit to Window");
            }
            ImGui::End();
        }
    }

    void endFrame(VkCommandBuffer commandBuffer) {
        ImVec4 originalWindowBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImVec4 originalText = ImGui::GetStyle().Colors[ImGuiCol_Text];

        // This gamma correction for ImGui was likely specific to a particular display setup
        // and might not be universally needed or correct. It's generally better to handle
        // gamma correction in the final display stage if possible, or ensure Vulkan swapchain
        // is sRGB if ImGui outputs sRGB colors.
        // For now, keeping it as it was in the original user-provided code base.
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(
            powf(originalWindowBg.x, 2.2f),
            powf(originalWindowBg.y, 2.2f),
            powf(originalWindowBg.z, 2.2f),
            originalWindowBg.w
        );
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(
            powf(originalText.x, 2.2f),
            powf(originalText.y, 2.2f),
            powf(originalText.z, 2.2f),
            originalText.w
        );

        ImGui::Render();

        // Restore original colors
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = originalWindowBg;
        ImGui::GetStyle().Colors[ImGuiCol_Text] = originalText;

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

} // namespace GuiOverlay
