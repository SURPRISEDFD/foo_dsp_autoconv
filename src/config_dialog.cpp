#include "config_dialog.h"
#include "resource.h"
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

namespace {

void set_text_utf8(HWND dlg, int id, const char * utf8) {
    pfc::stringcvt::string_wide_from_utf8 w(utf8);
    SetDlgItemTextW(dlg, id, w.get_ptr());
}

pfc::string8 get_text_utf8(HWND dlg, int id) {
    wchar_t buf[2048] = {};
    GetDlgItemTextW(dlg, id, buf, (int)(sizeof(buf) / sizeof(buf[0]) - 1));
    return pfc::string8(pfc::stringcvt::string_utf8_from_wide(buf));
}

void browse_folder(HWND dlg) {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = dlg;
    bi.lpszTitle = L"Select the folder that contains the calibration WAV files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path))
            SetDlgItemTextW(dlg, IDC_FOLDER, path);
        CoTaskMemFree(pidl);
    }
}

INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)lp);
        autoconv_preset * cfg = (autoconv_preset*)lp;
        CheckDlgButton(dlg, IDC_ENABLED,  cfg->enabled   ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_AUTOGAIN, cfg->auto_gain ? BST_CHECKED : BST_UNCHECKED);
        set_text_utf8(dlg, IDC_FOLDER,   cfg->folder);
        set_text_utf8(dlg, IDC_TEMPLATE, cfg->name_template);
        wchar_t g[64];
        swprintf_s(g, L"%.1f", (double)cfg->gain_db);
        SetDlgItemTextW(dlg, IDC_GAIN, g);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            browse_folder(dlg);
            return TRUE;
        case IDOK: {
            autoconv_preset * cfg = (autoconv_preset*)GetWindowLongPtrW(dlg, GWLP_USERDATA);
            cfg->enabled   = IsDlgButtonChecked(dlg, IDC_ENABLED)  == BST_CHECKED;
            cfg->auto_gain = IsDlgButtonChecked(dlg, IDC_AUTOGAIN) == BST_CHECKED;
            cfg->folder        = get_text_utf8(dlg, IDC_FOLDER);
            cfg->name_template = get_text_utf8(dlg, IDC_TEMPLATE);
            if (cfg->name_template.is_empty())
                cfg->name_template = "Calibration_{samplerate}.wav";
            {
                wchar_t buf[64] = {};
                GetDlgItemTextW(dlg, IDC_GAIN, buf, 63);
                double v = wcstod(buf, nullptr);
                if (v < -24.0) v = -24.0;
                if (v >  24.0) v =  24.0;
                cfg->gain_db = (float)v;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

bool run_config_dialog(HWND parent, autoconv_preset & cfg) {
    const INT_PTR r = DialogBoxParamW(
        core_api::get_my_instance(),
        MAKEINTRESOURCEW(IDD_CONFIG),
        parent, dlg_proc, (LPARAM)&cfg);
    return r == IDOK;
}
