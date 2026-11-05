package com.guessthedrawing.mobile

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.DialogInterface
import android.content.res.Configuration
import android.content.res.Resources
import android.os.LocaleList
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.TextView
import java.util.Locale

/** English source keys are stable; protocol values and user content are never rewritten. */
object L10n {
    val languages = listOf("en" to "English", "es" to "Español", "fr" to "Français", "de" to "Deutsch",
        "pt" to "Português", "it" to "Italiano", "ru" to "Русский", "zh" to "中文（简体）",
        "ja" to "日本語", "ko" to "한국어", "ar" to "العربية", "hi" to "हिन्दी")
    var language = "en"; private set
    val rtl get() = language == "ar"
    val locale get() = Locale.forLanguageTag(if (language == "zh") "zh-Hans" else language)
    private var resources: Resources? = null
    private val cache = mutableMapOf<String, String>()
    private val patterns by lazy { LocalizationKeys.ids.keys.filter { "%1" in it && it.any(Char::isLetter) }
        .sortedByDescending { it.length }.map { source ->
            val pieces = Regex("%([0-9]+)").findAll(source).toList()
            var end = 0
            val expression = buildString {
                append("^")
                pieces.forEach { append(Regex.escape(source.substring(end, it.range.first))); append("(.+?)"); end = it.range.last + 1 }
                append(Regex.escape(source.substring(end))); append("$")
            }
            Triple(source, Regex(expression, RegexOption.DOT_MATCHES_ALL), pieces.map { it.groupValues[1].toInt() })
        } }
    @Suppress("DEPRECATION")
    fun configure(activity: Activity, selected: String? = null) {
        val prefs = activity.getSharedPreferences("appearance", 0)
        val code = selected ?: prefs.getString("language", null) ?: Locale.getDefault().language
        language = code.takeIf { candidate -> languages.any { it.first == candidate } } ?: "en"
        if (selected != null) prefs.edit().putString("language", language).apply()
        val config = Configuration(activity.resources.configuration)
        config.setLocales(LocaleList(locale)); config.setLayoutDirection(locale)
        resources = activity.applicationContext.createConfigurationContext(config).resources
        activity.resources.updateConfiguration(config, activity.resources.displayMetrics)
        Locale.setDefault(locale); cache.clear()
        activity.window.decorView.layoutDirection = if (rtl) View.LAYOUT_DIRECTION_RTL else View.LAYOUT_DIRECTION_LTR
    }
    fun exact(source: String): String = cache.getOrPut(source) {
        val id = LocalizationKeys.ids[source]
        if (id == null || resources == null) source else resources!!.getString(id)
    }
    fun format(source: String, vararg arguments: Any?): String = Regex("([@#]?)%([0-9]+)").replace(exact(source)) {
        val value = arguments.getOrNull(it.groupValues[2].toInt() - 1)?.toString()
        if (value == null) it.value else isolate(it.groupValues[1] + value)
    }
    fun text(source: String): String {
        if (language == "en") return source
        if (LocalizationKeys.ids.containsKey(source)) return exact(source)
        for ((key, pattern, indices) in patterns) {
            val match = pattern.matchEntire(source) ?: continue
            val args = Array(indices.maxOrNull() ?: 0) { "" }
            indices.forEachIndexed { index, number -> args[number-1] = match.groupValues[index+1] }
            return format(key, *args)
        }
        return source
    }
    fun detail(value: String) = value.split(" · ").joinToString(" · ") {
        if (it.matches(Regex("#[0-9]+"))) isolate(it) else text(it)
    }
    fun isolate(value: String) = if (rtl && value.isNotEmpty()) "\u2068$value\u2069" else value
    fun digits(value: String) = value.map { ch -> Character.digit(ch, 10).takeIf { it >= 0 }?.let { ('0'.code + it).toChar() } ?: ch }.joinToString("")
    fun options(context: Context, values: List<String>, label: (String) -> String = ::text) = object : ArrayAdapter<String>(context, android.R.layout.simple_spinner_dropdown_item, values) {
        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View =
            (super.getView(position, convertView, parent) as TextView).apply { text = label(values[position]) }
        override fun getDropDownView(position: Int, convertView: View?, parent: ViewGroup): View =
            (super.getDropDownView(position, convertView, parent) as TextView).apply { text = label(values[position]) }
    }
}

class LocalizedDialogBuilder(context: Context) : AlertDialog.Builder(context) {
    fun setRawTitle(value: String): LocalizedDialogBuilder { super.setTitle(value); return this }
    override fun setTitle(title: CharSequence?): AlertDialog.Builder = super.setTitle(title?.toString()?.let(L10n::text))
    override fun setMessage(message: CharSequence?): AlertDialog.Builder = super.setMessage(message?.toString()?.let(L10n::text))
    override fun setPositiveButton(text: CharSequence?, listener: DialogInterface.OnClickListener?): AlertDialog.Builder = super.setPositiveButton(text?.toString()?.let(L10n::text), listener)
    override fun setNegativeButton(text: CharSequence?, listener: DialogInterface.OnClickListener?): AlertDialog.Builder = super.setNegativeButton(text?.toString()?.let(L10n::text), listener)
    override fun setNeutralButton(text: CharSequence?, listener: DialogInterface.OnClickListener?): AlertDialog.Builder = super.setNeutralButton(text?.toString()?.let(L10n::text), listener)
    override fun setItems(items: Array<out CharSequence>?, listener: DialogInterface.OnClickListener?): AlertDialog.Builder = super.setItems(items?.map { L10n.text(it.toString()) }?.toTypedArray(), listener)
    override fun setSingleChoiceItems(items: Array<out CharSequence>?, checkedItem: Int, listener: DialogInterface.OnClickListener?): AlertDialog.Builder = super.setSingleChoiceItems(items?.map { L10n.text(it.toString()) }?.toTypedArray(), checkedItem, listener)
}
