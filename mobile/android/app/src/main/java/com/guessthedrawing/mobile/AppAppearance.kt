package com.guessthedrawing.mobile

import android.app.Activity
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.widget.*

class AppAppearance(private val activity: Activity) {
    private val prefs = activity.getSharedPreferences("appearance", 0)
    var selection = prefs.getString("theme", "system") ?: "system"; private set
    var palette = ThemePalette.all.first(); private set
    fun select(id: String) { selection = id; prefs.edit().putString("theme", id).apply(); configure() }
    @android.annotation.SuppressLint("ResourceType") // Indices refer to the explicit local attribute array below.
    fun configure() {
        val dark = activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
        val chosen = ThemePalette.all.firstOrNull { it.id == selection }
        val resource = if (chosen != null) activity.resources.getIdentifier("Gtd_"+chosen.id.replace('-', '_'), "style", activity.packageName)
            else if (dark) android.R.style.Theme_Material_NoActionBar else android.R.style.Theme_Material_Light_NoActionBar
        activity.theme.setTo(activity.resources.newTheme().apply { applyStyle(resource, true) })
        palette = chosen ?: run {
            val context = ContextThemeWrapper(activity, resource)
            val attrs = intArrayOf(android.R.attr.colorBackground, android.R.attr.colorBackgroundFloating,
                android.R.attr.textColorPrimary, android.R.attr.colorAccent, android.R.attr.colorButtonNormal, android.R.attr.textColorSecondary)
            val colors = context.obtainStyledAttributes(attrs)
            val p = ThemePalette("system", "System", colors.getColor(0, Color.WHITE), colors.getColor(1, Color.WHITE),
                colors.getColor(1, Color.WHITE), colors.getColor(2, Color.BLACK), colors.getColor(3, Color.BLUE),
                if (dark) Color.BLACK else Color.WHITE, colors.getColor(4, Color.LTGRAY), colors.getColor(5, Color.GRAY))
            colors.recycle(); p
        }
        @Suppress("DEPRECATION")
        activity.window.decorView.systemUiVisibility = if (palette.dark) 0 else
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
    }
    private fun states(normal: Int, disabled: Int) = ColorStateList(arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()), intArrayOf(disabled, normal))
    fun apply(view: View) {
        if (view is DrawingView || view.tag == "ink") return
        val p = palette
        if (view.tag == "card") view.background = GradientDrawable().apply {
            setColor(p.surface); cornerRadius = 16 * activity.resources.displayMetrics.density
            setStroke(1, p.disabledText and 0x00ffffff or 0x33000000)
        }
        if (view is TextView) {
            view.setTextColor(states(p.text, p.disabledText)); view.setHintTextColor(p.disabledText); view.setLinkTextColor(p.accent)
        }
        when (view) {
            is Toolbar -> {
                view.setBackgroundColor(p.surface); view.setTitleTextColor(p.text); view.setSubtitleTextColor(p.disabledText)
                view.overflowIcon?.mutate()?.setTint(p.text); view.navigationIcon?.mutate()?.setTint(p.text)
            }
            is Switch -> { view.thumbTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(p.accent, p.disabledText)); view.trackTintList = ColorStateList.valueOf(p.button) }
            is CompoundButton -> view.buttonTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(p.accent, p.disabledText))
            is Button -> {
                val fill = if (view.tag == "primary") p.accent else p.button
                val shape = GradientDrawable().apply { setColor(fill); cornerRadius = 12 * activity.resources.displayMetrics.density }
                view.backgroundTintList = null
                view.background = android.graphics.drawable.RippleDrawable(ColorStateList.valueOf(p.accent and 0x00ffffff or 0x33000000), shape, null)
                view.setPadding((12 * activity.resources.displayMetrics.density).toInt(), 0, (12 * activity.resources.displayMetrics.density).toInt(), 0)
                if (view.tag == "primary") view.setTextColor(states(p.accentText, p.disabledText))
            }
            is EditText -> view.backgroundTintList = states(p.accent, p.disabledText)
            is ProgressBar -> {
                view.indeterminateTintList = ColorStateList.valueOf(p.accent)
                view.progressTintList = ColorStateList.valueOf(p.accent)
            }
        }
        if (view is ViewGroup) for (i in 0 until view.childCount) apply(view.getChildAt(i))
    }
}
