package com.datfooldive.warriors;

import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

// The layout editor's own bar: the screen stays the game's, so a finger anywhere else still drags a control, and only
// the strip itself takes a touch. Done writes the layout out (native/touch.cpp does that as the drag ends too), and
// Reset puts the pad's own back.
class LayoutEditor {

    private final WarriorsActivity activity;
    private View bar;

    LayoutEditor(WarriorsActivity activity) {
        this.activity = activity;
    }

    void show() {
        if (bar != null) return;
        LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(7), dp(14), dp(7));
        row.setClickable(true);  // a tap on the bar itself is not a tap on the game behind it
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xF00F0D0D);
        bg.setCornerRadius(dp(10));
        bg.setStroke(dp(2), 0xFFB8241A);
        row.setBackground(bg);

        TextView hint = new TextView(activity);
        hint.setText("DRAG A CONTROL TO MOVE IT");
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        hint.setTextColor(0xFFEBB85C);
        row.addView(hint, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView reset = button("RESET");
        reset.setOnClickListener(x -> WarriorsActivity.nativeEditReset());
        TextView done = button("DONE");
        done.setTextColor(0xFFEBB85C);
        done.setOnClickListener(x -> {
            WarriorsActivity.nativeEditEnd();
            hide();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(92), dp(34));
        lp.setMargins(dp(8), 0, 0, 0);
        row.addView(reset, lp);
        LinearLayout.LayoutParams lp2 = new LinearLayout.LayoutParams(dp(92), dp(34));
        lp2.setMargins(dp(8), 0, 0, 0);
        row.addView(done, lp2);

        FrameLayout.LayoutParams at = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        at.bottomMargin = dp(26);
        ((ViewGroup) activity.getWindow().getDecorView()).addView(row, at);
        bar = row;
    }

    void hide() {
        if (bar == null) return;
        ((ViewGroup) bar.getParent()).removeView(bar);
        bar = null;
    }

    private TextView button(String label) {
        TextView t = new TextView(activity);
        t.setText(label);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        t.setTextColor(0xFFEDE8E0);
        t.setGravity(Gravity.CENTER);
        t.setClickable(true);
        t.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFF292423);
        bg.setCornerRadius(dp(6));
        t.setBackground(bg);
        return t;
    }

    private int dp(float value) {
        return (int) (value * activity.getResources().getDisplayMetrics().density + 0.5f);
    }
}
