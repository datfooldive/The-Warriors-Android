package com.datfooldive.warriors;

import android.app.Dialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.Locale;

// The settings panel, drawn by Android rather than by the game: the activity's own dialog over the running picture,
// dark with the game's red and gold. The values live in native/settings_bridge.cpp, which this reads once when it
// opens and writes back one at a time; a slider only writes the file when it is let go, not on every step of a drag.
class SettingsDialog extends Dialog {

    // the numbers native/settings_bridge.cpp reads and writes, in its order
    static final int RESOLUTION = 0, ANTI_ALIAS = 1, ANISO = 2, VSYNC = 3, FRAME_SKIP = 4, ASPECT = 5, FPS = 6,
            HDR = 7, HDR_AVAILABLE = 8, SCALE = 9, OPACITY = 10, OUT_W = 11, OUT_H = 12, CAMERA = 13, CAMERA_MODE = 14, COUNT = 15;

    private static final int BG = 0xFF0F0D0D;
    private static final int BORDER = 0xFFB8241A;
    private static final int GOLD = 0xFFEBB85C;
    private static final int TEXT = 0xFFEDE8E0;
    private static final int DIM = 0xFF9A9188;
    private static final int CHIP = 0xFF292423;
    private static final int CHIP_ON = 0xFF3A2B27;

    private static final String[] kAntiAlias = {"OFF", "SMAA", "MSAA 2X", "MSAA 4X"};
    private static final String[] kAniso = {"GAME DEFAULT", "2X", "4X", "8X", "16X"};
    private static final int[] kAnisoValues = {0, 2, 4, 8, 16};
    private static final String[] kAspect = {"ORIGINAL", "16:9", "STRETCH", "FILL"};
    private static final String[] kCameraMode = {"FLOATING", "FIXED", "SWIPE"};

    private final WarriorsActivity activity;  // the dialog's own context is a wrapper around it, not it
    private final int[] v = new int[COUNT];
    private final SeekBar[] bars = new SeekBar[COUNT];
    private final TextView[] numbers = new TextView[COUNT];
    private boolean loading;  // a reload filling the widgets is not a change to write back

    private final TextView[] tabs = new TextView[4];
    private final LinearLayout[] pages = new LinearLayout[4];

    private TextView antiAliasText, anisoText, aspectText, cameraModeText;
    private TextView vsync, frameSkip, fps, hdr;

    SettingsDialog(WarriorsActivity activity) {
        super(activity);
        this.activity = activity;
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setCanceledOnTouchOutside(true);

        LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(12), dp(18), dp(12));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(BG);
        bg.setCornerRadius(dp(10));
        bg.setStroke(dp(2), BORDER);
        root.setBackground(bg);

        TextView title = text("SETTINGS", 17, GOLD);
        title.setGravity(Gravity.CENTER);
        title.setLetterSpacing(0.14f);
        root.addView(title);
        root.addView(line());

        LinearLayout bar = new LinearLayout(activity);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(0, dp(9), 0, dp(6));
        String[] names = {"GRAPHICS", "PERFORMANCE", "DISPLAY", "CONTROLS"};
        for (int i = 0; i < 4; i++) {
            final int at = i;
            tabs[i] = chip(names[i], 12);
            tabs[i].setOnClickListener(x -> select(at));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            lp.setMargins(dp(2), 0, dp(2), 0);
            bar.addView(tabs[i], lp);
        }
        root.addView(bar);

        for (int i = 0; i < 4; i++) {
            pages[i] = new LinearLayout(activity);
            pages[i].setOrientation(LinearLayout.VERTICAL);
            pages[i].setVisibility(View.GONE);
        }
        graphics(pages[0]);
        performance(pages[1]);
        display(pages[2]);
        controls(pages[3]);

        LinearLayout holder = new LinearLayout(activity);
        holder.setOrientation(LinearLayout.VERTICAL);
        for (LinearLayout p : pages) holder.addView(p);
        ScrollView scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(holder);
        root.addView(scroll);

        root.addView(line());
        root.addView(footer());

        setContentView(root);
        select(0);
        reload();
    }

    // the panel a fixed share of the screen wide, as tall as its rows ask for
    @Override
    protected void onStart() {
        super.onStart();
        Window window = getWindow();
        if (window == null) return;
        window.setBackgroundDrawable(new ColorDrawable(0));
        window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        WindowManager.LayoutParams lp = window.getAttributes();
        lp.dimAmount = 0.65f;
        lp.gravity = Gravity.CENTER;
        int screenW = getContext().getResources().getDisplayMetrics().widthPixels;
        lp.width = Math.min((int) (screenW * 0.62f), dp(660));
        lp.height = WindowManager.LayoutParams.WRAP_CONTENT;
        window.setAttributes(lp);
    }

    // ---------------------------------------------------------------- the rows

    private void graphics(LinearLayout box) {
        slider(box, "RENDER RESOLUTION", RESOLUTION, 20, 200, 5);
        antiAliasText = stepper(box, "ANTI-ALIASING", kAntiAlias, ANTI_ALIAS);
        anisoText = stepper(box, "ANISOTROPIC FILTERING", kAniso, ANISO);
    }

    private void performance(LinearLayout box) {
        vsync = toggle(box, "VERTICAL SYNC", VSYNC);
        frameSkip = toggle(box, "FRAME SKIP", FRAME_SKIP);
    }

    private void display(LinearLayout box) {
        aspectText = stepper(box, "ASPECT RATIO", kAspect, ASPECT);
        fps = toggle(box, "FPS COUNTER", FPS);
        hdr = toggle(box, "HDR", HDR);
    }

    private void controls(LinearLayout box) {
        slider(box, "BUTTON SIZE", SCALE, 50, 150, 1);
        slider(box, "BUTTON OPACITY", OPACITY, 20, 100, 1);
        cameraModeText = stepper(box, "RIGHT STICK", kCameraMode, CAMERA_MODE);
        slider(box, "CAMERA SENSITIVITY", CAMERA, 50, 300, 5);

        TextView edit = chip("MOVE CONTROLS", 14);
        edit.setTextColor(GOLD);
        GradientDrawable accent = new GradientDrawable();
        accent.setColor(0xFF7A2E24);
        accent.setCornerRadius(dp(6));
        edit.setBackground(accent);
        edit.setPadding(0, dp(9), 0, dp(9));
        edit.setOnClickListener(x -> {
            activity.beginLayoutEdit();
            dismiss();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(10);
        box.addView(edit, lp);
    }

    // a row whose value is dragged: the label and the value on one line, the bar under them
    private void slider(LinearLayout box, String label, int key, int min, int max, int step) {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(5), 0, dp(3));

        LinearLayout head = new LinearLayout(getContext());
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.addView(text(label, 13, TEXT), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView value = text("", 13, GOLD);
        head.addView(value);
        row.addView(head);

        SeekBar bar = new SeekBar(getContext());
        bar.setMax((max - min) / step);
        bar.setPadding(dp(2), 0, dp(2), 0);
        bar.setProgressTintList(ColorStateList.valueOf(GOLD));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(CHIP_ON));
        bar.setThumbTintList(ColorStateList.valueOf(GOLD));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                if (loading) return;
                WarriorsActivity.nativeSettingsSet(key, min + progress * step, false);
                show(key, min, step);
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {}

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                WarriorsActivity.nativeSettingsSet(key, min + s.getProgress() * step, true);
            }
        });
        row.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(30)));
        box.addView(row);
        bars[key] = bar;
        numbers[key] = value;
    }

    // a slider's value beside its label: the percentage, and for the resolution the pixels it makes
    private void show(int key, int min, int step) {
        int value = min + bars[key].getProgress() * step;
        if (key == RESOLUTION)
            numbers[key].setText(String.format(Locale.US, "%d%% (%dx%d)", value, v[OUT_W] * value / 100,
                    v[OUT_H] * value / 100));
        else numbers[key].setText(String.format(Locale.US, "%d%%", value));
    }

    private TextView stepper(LinearLayout box, String label, String[] names, int key) {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(5), 0, dp(5));
        row.addView(text(label, 13, TEXT), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView value = text(names[0], 13, GOLD);
        value.setGravity(Gravity.CENTER);
        value.setMinWidth(dp(96));
        value.setPadding(dp(6), 0, dp(6), 0);

        TextView down = chip("\u2039", 16);
        TextView up = chip("\u203A", 16);
        down.setOnClickListener(x -> step(key, -1, names.length, value, names));
        up.setOnClickListener(x -> step(key, 1, names.length, value, names));
        row.addView(down, new LinearLayout.LayoutParams(dp(34), dp(30)));
        row.addView(value, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(30)));
        row.addView(up, new LinearLayout.LayoutParams(dp(34), dp(30)));
        box.addView(row);
        return value;
    }

    private void step(int key, int delta, int count, TextView value, String[] names) {
        int i = (index(key, count) + delta) % count;
        if (i < 0) i += count;
        int written = key == ANISO ? kAnisoValues[i] : i;
        v[key] = written;
        WarriorsActivity.nativeSettingsSet(key, written, true);
        value.setText(names[i]);
    }

    // the index the setting's value sits at in its table
    private int index(int key, int count) {
        if (key == ANISO) {
            for (int i = 0; i < kAnisoValues.length; i++)
                if (kAnisoValues[i] == v[ANISO]) return i;
            return 0;
        }
        return Math.max(0, Math.min(v[key], count - 1));
    }

    // A switch of this panel's own: android.widget.Switch draws nothing at all under the game's bare theme
    // (Theme.NoTitleBar.Fullscreen has no switch style, so the thumb and track come out empty), and the state reads
    // better as a word anyway — gold while it is on
    private TextView toggle(LinearLayout box, String label, int key) {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(4), 0, dp(4));
        row.addView(text(label, 13, TEXT), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView sw = text("", 12, DIM);
        sw.setGravity(Gravity.CENTER);
        sw.setMinWidth(dp(62));
        sw.setPadding(dp(8), dp(5), dp(8), dp(5));
        sw.setClickable(true);
        sw.setFocusable(true);
        sw.setOnClickListener(x -> {
            if (loading) return;
            boolean on = !Boolean.TRUE.equals(sw.getTag());
            paint(sw, on);
            WarriorsActivity.nativeSettingsSet(key, on ? 1 : 0, true);
        });
        paint(sw, false);
        row.addView(sw);
        box.addView(row);
        return sw;
    }

    // the switch as it reads for a state, whether that came from a tap or from the file
    private void paint(TextView sw, boolean on) {
        sw.setTag(on);
        sw.setText(on ? "ON" : "OFF");
        sw.setTextColor(on ? GOLD : DIM);
        sw.setBackground(chipBackground(on));
    }

    // ---------------------------------------------------------------- the panel

    private void select(int at) {
        for (int i = 0; i < 4; i++) {
            pages[i].setVisibility(i == at ? View.VISIBLE : View.GONE);
            tabs[i].setTextColor(i == at ? GOLD : DIM);
            tabs[i].setBackground(chipBackground(i == at));
        }
    }

    private LinearLayout footer() {
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(9), 0, 0);
        row.addView(text("Changes are saved as you make them", 11, DIM),
                new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView back = chip("BACK", 13);
        back.setOnClickListener(x -> dismiss());
        TextView defaults = chip("DEFAULTS", 13);
        defaults.setTextColor(GOLD);
        defaults.setOnClickListener(x -> {
            WarriorsActivity.nativeSettingsDefaults();
            reload();
        });
        LinearLayout.LayoutParams one = new LinearLayout.LayoutParams(dp(96), dp(34));
        one.setMargins(dp(6), 0, 0, 0);
        LinearLayout.LayoutParams two = new LinearLayout.LayoutParams(dp(96), dp(34));
        two.setMargins(dp(6), 0, 0, 0);
        row.addView(back, one);
        row.addView(defaults, two);
        return row;
    }

    // the values as the file has them now, into the widgets
    private void reload() {
        System.arraycopy(WarriorsActivity.nativeSettings(), 0, v, 0, COUNT);
        loading = true;
        bars[RESOLUTION].setProgress((v[RESOLUTION] - 20) / 5);
        show(RESOLUTION, 20, 5);
        antiAliasText.setText(kAntiAlias[Math.min(v[ANTI_ALIAS], 3)]);
        anisoText.setText(kAniso[index(ANISO, kAniso.length)]);
        paint(vsync, v[VSYNC] != 0);
        paint(frameSkip, v[FRAME_SKIP] != 0);
        aspectText.setText(kAspect[Math.min(v[ASPECT], 3)]);
        paint(fps, v[FPS] != 0);
        paint(hdr, v[HDR] != 0);
        hdr.setEnabled(v[HDR_AVAILABLE] != 0);
        hdr.setAlpha(v[HDR_AVAILABLE] != 0 ? 1f : 0.4f);
        bars[SCALE].setProgress(v[SCALE] - 50);
        show(SCALE, 50, 1);
        bars[OPACITY].setProgress(v[OPACITY] - 20);
        show(OPACITY, 20, 1);
        bars[CAMERA].setProgress((v[CAMERA] - 50) / 5);
        show(CAMERA, 50, 5);
        cameraModeText.setText(kCameraMode[Math.min(v[CAMERA_MODE], 2)]);
        loading = false;
    }

    // ---------------------------------------------------------------- the pieces

    private TextView text(String s, float sp, int color) {
        TextView t = new TextView(getContext());
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        return t;
    }

    private TextView chip(String s, float sp) {
        TextView t = text(s, sp, TEXT);
        t.setGravity(Gravity.CENTER);
        t.setBackground(chipBackground(false));
        t.setClickable(true);
        t.setFocusable(true);
        t.setPadding(dp(8), dp(5), dp(8), dp(5));
        return t;
    }

    private GradientDrawable chipBackground(boolean on) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(on ? CHIP_ON : CHIP);
        d.setCornerRadius(dp(6));
        if (on) d.setStroke(dp(1), BORDER);
        return d;
    }

    private View line() {
        View rule = new View(getContext());
        rule.setBackgroundColor(0x59EBB85C);
        rule.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));
        return rule;
    }

    private int dp(float value) {
        return (int) (value * getContext().getResources().getDisplayMetrics().density + 0.5f);
    }
}
