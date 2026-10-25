package ru.ukwood.gyvertwink

import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue
import android.view.View
import android.widget.Button
import android.widget.TextView

/** Цвета и мелкие помощники для экранов, нарисованных кодом (в духе веб-интерфейса: тёмный фон, янтарный акцент). */
object Ui {
    val bg = Color.parseColor("#12151C")
    val card = Color.parseColor("#1B2030")
    val line = Color.parseColor("#2B3247")
    val fg = Color.parseColor("#E8ECF5")
    val muted = Color.parseColor("#8D96AD")
    val accent = Color.parseColor("#FFB020")
    val accentText = Color.parseColor("#1A1300")
    val bad = Color.parseColor("#FF5D5D")

    fun dp(ctx: Context, v: Int): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), ctx.resources.displayMetrics).toInt()

    private fun rounded(ctx: Context, fill: Int, stroke: Int?): GradientDrawable = GradientDrawable().apply {
        setColor(fill)
        cornerRadius = dp(ctx, 10).toFloat()
        if (stroke != null) setStroke(dp(ctx, 1), stroke)
    }

    /** Кнопка не ниже 48 dp (удобно нажимать пальцем). primary - янтарная заливка. */
    fun button(ctx: Context, label: String, primary: Boolean, onClick: () -> Unit): Button = Button(ctx).apply {
        text = label
        isAllCaps = false
        textSize = 16f
        minHeight = dp(ctx, 48)
        minimumHeight = dp(ctx, 48)
        setTextColor(if (primary) accentText else fg)
        background = rounded(ctx, if (primary) accent else card, if (primary) null else line)
        setPadding(dp(ctx, 16), 0, dp(ctx, 16), 0)
        setOnClickListener { onClick() }
    }

    fun label(ctx: Context, s: String, sizeSp: Float, color: Int, bold: Boolean = false): TextView = TextView(ctx).apply {
        text = s
        textSize = sizeSp
        setTextColor(color)
        if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
    }

    fun gap(ctx: Context, dpH: Int): View = View(ctx).apply {
        layoutParams = android.widget.LinearLayout.LayoutParams(1, dp(ctx, dpH))
    }
}
