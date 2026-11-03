package com.guessthedrawing.mobile

import android.Manifest
import android.app.AlertDialog
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import org.json.JSONArray
import org.json.JSONObject
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import kotlin.concurrent.thread

class MainActivity : ComponentActivity() {
    internal val client = GameClient()
    internal val voice by lazy { VoiceChat(this, client) }
    internal val appearance by lazy { AppAppearance(this) }
    internal val communityUI by lazy { CommunityUI(this) }
    private var socialButton: Button? = null
    private var micButton: Button? = null
    private var audioButton: Button? = null
    private var curveControls: View? = null
    private var curveHint: TextView? = null
    private var drawingToolbar: View? = null
    private lateinit var root: LinearLayout
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private var screen = ""
    private var lobbyTab = 0
    private var gameTab = 0
    private var lastList = ""
    private var lastRound = ""
    private var lastDrawingAllowed = false
    private var authButton: Button? = null
    private var listArea: LinearLayout? = null
    private var board: DrawingView? = null
    private var roundLabel: TextView? = null
    private var wordLabel: TextView? = null
    private var sendButton: Button? = null
    private var guessSwitch: Switch? = null
    private var messageInput: EditText? = null
    private var exportBitmap: Bitmap? = null
    private val microphonePermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted && client.canSpeak) enableMic()
        else client.notice("Microphone permission is off. You can still play and listen.")
    }
    private val exportDrawing = registerForActivityResult(ActivityResultContracts.CreateDocument("image/png")) { uri ->
        val bitmap = exportBitmap; exportBitmap = null
        if (uri == null || bitmap == null) bitmap?.recycle()
        else thread(name = "gtd-export") {
            val result = runCatching {
                val stream = contentResolver.openOutputStream(uri) ?: error("Cannot open the chosen file")
                stream.use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            }
            bitmap.recycle()
            runOnUiThread { if (result.isFailure && !isDestroyed) client.notice("Could not export the drawing.") }
        }
    }
    private val prefs by lazy { getSharedPreferences("connection", MODE_PRIVATE) }
    internal fun dp(n: Int) = (resources.displayMetrics.density * n).toInt()
    internal fun column() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
    internal fun row() = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
    internal fun label(text: String, size: Float = 16f) = TextView(this).apply {
        this.text = text; textSize = size; setTextColor(appearance.palette.text); setPadding(dp(8), dp(6), dp(8), dp(6))
    }
    internal fun button(text: String, action: () -> Unit) = Button(this).apply {
        this.text = text; isAllCaps = false; minHeight = dp(48); setOnClickListener { action() }
        appearance.apply(this)
    }
    internal fun input(hint: String, value: String = "", password: Boolean = false, limit: Int = 128) = EditText(this).apply {
        this.hint = hint; setText(value); setSingleLine(); minHeight = dp(52)
        filters = arrayOf(android.text.InputFilter.LengthFilter(limit))
        inputType = if (password) InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            else InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        setPadding(dp(12), dp(6), dp(12), dp(6))
        appearance.apply(this)
    }
    internal fun scroll(content: View) = ScrollView(this).apply { isFillViewport = true; addView(content) }
    private fun weighted(parent: LinearLayout, view: View) = parent.addView(view, LinearLayout.LayoutParams(0, -2, 1f))
    override fun onCreate(savedInstanceState: Bundle?) {
        appearance.configure()
        super.onCreate(savedInstanceState)
        volumeControlStream = android.media.AudioManager.STREAM_VOICE_CALL
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                when (screen) { "game" -> confirmLeave(); "lobby" -> client.logout(); else -> finish() }
            }
        })
        client.changed = { render() }
        client.voiceReceived = { voice.receive(it) }
        voice.changed = { updateVoiceControls() }
        buildScreen(); render()
    }
    private fun buildScreen() {
        screen = client.page; lastList = ""; board = null; listArea = null; authButton = null
        root = column().apply { setBackgroundColor(appearance.palette.window) }
        micButton = null; audioButton = null; curveControls = null; curveHint = null; socialButton = null; drawingToolbar = null
        root.setOnApplyWindowInsetsListener { v, insets ->
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                val area = insets.getInsets(android.view.WindowInsets.Type.systemBars() or android.view.WindowInsets.Type.ime())
                v.setPadding(area.left, area.top, area.right, area.bottom)
            } else {
                @Suppress("DEPRECATION")
                v.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            insets
        }
        setContentView(root)
        val toolbar = Toolbar(this).apply {
            title = if (screen == "game") client.room.optString("roomName") else "Guess The Drawing"
            setTitleTextColor(appearance.palette.text); setBackgroundColor(appearance.palette.surface)
        }
        toolbar.menu.add("Theme").setOnMenuItemClickListener { chooseTheme(); true }
        if (screen != "login") {
            toolbar.menu.add(if (screen == "game") "Room options" else "Account").setOnMenuItemClickListener {
                if (screen == "game") roomOptions(toolbar) else accountOptions(toolbar)
                true
            }
        }
        root.addView(toolbar)
        if (screen == "lobby") {
            socialButton = button("People & messages") { communityUI.hub() }
            root.addView(socialButton)
        }
        status = label(client.status, 13f); root.addView(status)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { isIndeterminate = true }
        root.addView(progress, LinearLayout.LayoutParams(-1, dp(4)))
        when (screen) { "login" -> login(); "lobby" -> lobby(); "game" -> game() }
        appearance.apply(root)
        root.requestApplyInsets()
    }
    private fun login() {
        val form = column().apply { tag = "card"; setPadding(dp(18), dp(18), dp(18), dp(24)) }
        form.addView(ImageView(this).apply { setImageResource(R.drawable.ic_launcher); contentDescription = "Guess The Drawing" }, LinearLayout.LayoutParams(dp(64), dp(64)))
        form.addView(label("Draw together.\nGuess together.", 30f))
        form.addView(label("Sign in to play with desktop and mobile friends.", 16f))
        val host = input("Master server address", prefs.getString("host", "") ?: "", limit = 253)
        val port = input("Port", prefs.getInt("port", 45450).toString(), limit = 5).apply { inputType = InputType.TYPE_CLASS_NUMBER }
        val user = input("Username", prefs.getString("user", "") ?: "", limit = 24)
        val password = input("Password", password = true)
        val name = input("Display name", limit = 32).apply { visibility = View.GONE }
        val create = CheckBox(this).apply {
            text = "Create an account"; minHeight = dp(48)
            setOnCheckedChangeListener { _, checked -> name.visibility = if (checked) View.VISIBLE else View.GONE }
        }
        listOf(host, port, user, password, create, name).forEach { form.addView(it) }
        authButton = button("Continue") {
            val number = port.text.toString().toIntOrNull() ?: 0
            if (number in 1..65535) prefs.edit().putString("host", host.text.toString().trim())
                .putInt("port", number).putString("user", user.text.toString()).apply()
            client.authenticate(host.text.toString(), number, user.text.toString(), password.text.toString(),
                name.text.toString(), create.isChecked)
            password.text.clear()
        }
        form.addView(authButton)
        authButton?.tag = "primary"
        form.addView(label("Use the host computer's LAN address. For an Android emulator, use 10.0.2.2.\nGame version " + BuildConfig.GAME_VERSION, 13f))
        root.addView(scroll(form), LinearLayout.LayoutParams(-1, 0, 1f))
    }
    private fun tabs(names: List<String>, selection: Int, choose: (Int) -> Unit): LinearLayout {
        val group = row().apply { setPadding(dp(8), dp(5), dp(8), dp(5)) }
        names.forEachIndexed { i, name ->
            val item = button(name) {
                for (j in 0 until group.childCount) group.getChildAt(j).apply { tag = if (j == i) "primary" else null; isSelected = j == i }
                appearance.apply(group); choose(i)
            }.apply { tag = if (i == selection) "primary" else null; isSelected = i == selection }
            group.addView(item, LinearLayout.LayoutParams(0, dp(44), 1f).apply { setMargins(dp(3), 0, dp(3), 0) })
        }
        appearance.apply(group); return group
    }
    private fun lobby() {
        root.addView(tabs(listOf("Rooms", "Ranks", "Profile"), lobbyTab) { lobbyTab = it; buildScreen(); render() })
        val area = column().apply { setPadding(dp(10), 0, dp(10), dp(12)) }
        root.addView(scroll(area), LinearLayout.LayoutParams(-1, 0, 1f))
        if (lobbyTab == 2) profile(area) else {
            listArea = area
            area.addView(button("Refresh") { client.refreshRooms(); client.refreshLeaderboards() })
            refreshList()
        }
    }
    private fun profile(area: LinearLayout) {
        val profile = client.profile
        area.addView(label("@" + client.username, 24f))
        area.addView(label("Total " + profile.optInt("totalScore") + " · Guessing " + profile.optInt("guessingScore") +
            " · Drawing " + profile.optInt("drawingScore")))
        val name = input("Display name", profile.optString("displayName"), limit = 32)
        val country = Spinner(this)
        val codes = (assets.list("country-flags")?.map { it.substringBefore(".").uppercase() } ?: emptyList()).sorted()
        val options = listOf("No country") + codes
        country.adapter = object : ArrayAdapter<String>(this, android.R.layout.simple_spinner_dropdown_item, options) {
            private fun option(position: Int): View = row().apply {
                minimumHeight = dp(48); tag = "card"
                if (position > 0) runCatching {
                    val flag = ImageView(this@MainActivity)
                    assets.open("country-flags/${options[position].lowercase()}.png").use { flag.setImageDrawable(android.graphics.drawable.Drawable.createFromStream(it, null)) }
                    flag.contentDescription = options[position]; addView(flag, LinearLayout.LayoutParams(dp(28), dp(22)))
                }
                addView(label(options[position])); appearance.apply(this)
            }
            override fun getView(position: Int, convertView: View?, parent: ViewGroup) = option(position)
            override fun getDropDownView(position: Int, convertView: View?, parent: ViewGroup) = option(position)
        }
        country.setSelection(maxOf(0, options.indexOf(profile.optString("countryCode"))))
        val old = input("Current password (only to change password)", password = true)
        val new = input("New password (optional)", password = true)
        listOf(name, country, old, new).forEach { area.addView(it) }
        area.addView(button("Save profile") {
            client.saveProfile(name.text.toString(), if (country.selectedItemPosition == 0) "" else country.selectedItem.toString(),
                old.text.toString(), new.text.toString())
            old.text.clear(); new.text.clear()
        })
    }
    private fun game() {
        roundLabel = label("", 17f); wordLabel = label("", 18f)
        root.addView(roundLabel); root.addView(wordLabel)
        val canvas = DrawingView(this, client); board = canvas
        canvas.requestText = { x, y -> prompt("Add text", false) { canvas.addText(x, y, it) } }
        canvas.background = GradientDrawable().apply { setColor(Color.WHITE); cornerRadius = dp(14).toFloat() }
        canvas.clipToOutline = true
        root.addView(canvas, LinearLayout.LayoutParams(-1, 0, 1.3f).apply { setMargins(dp(10), dp(4), dp(10), dp(4)) })
        curveHint = label("Drag the endpoints, then drag the line to bend it.", 12f).apply { visibility = View.GONE }
        root.addView(curveHint)
        curveControls = row().also { controls ->
            controls.addView(button("Finish curve") { canvas.finishCurve() })
            controls.addView(button("Cancel") { canvas.cancelCurve() })
            controls.visibility = View.GONE; root.addView(controls)
        }
        canvas.editingChanged = {
            curveHint?.visibility = if (canvas.tool == "curve" && client.canDraw) View.VISIBLE else View.GONE
            curveControls?.visibility = if (canvas.curvePending) View.VISIBLE else View.GONE
        }
        val tools = row()
        val names = listOf("Pen", "Brush", "Airbrush", "Curve", "Polygon", "Line", "Rectangle", "Rounded rectangle", "Ellipse", "Move", "Object eraser", "Fill", "Pick color", "Text")
        val types = listOf("freehand", "brush", "airbrush", "curve", "polygon", "line", "rectangle", "rounded_rectangle", "ellipse", "move", "eraser", "fill", "pick", "text")
        tools.addView(button("Tools") {
            AlertDialog.Builder(this).setTitle("Drawing tool").setItems(names.toTypedArray()) { _, index -> canvas.tool = types[index] }.show()
        })
        tools.addView(button("Color") {
            val colors = arrayOf("#202124", "#FFFFFF", "#EF4444", "#F59E0B", "#FACC15", "#22C55E", "#06B6D4", "#3B82F6", "#8B5CF6", "#EC4899")
            val grid = GridLayout(this).apply { columnCount = 5; setPadding(dp(12), dp(12), dp(12), dp(12)) }
            val dialog = AlertDialog.Builder(this).setTitle("Ink color").setView(grid).create()
            colors.forEach { color -> grid.addView(button(" ") { canvas.ink = color; dialog.dismiss() }.apply {
                tag = "ink"; contentDescription = color; backgroundTintList = android.content.res.ColorStateList.valueOf(Color.parseColor(color))
            }, ViewGroup.LayoutParams(dp(52), dp(52))) }
            dialog.show()
        })
        tools.addView(button("Size") {
            AlertDialog.Builder(this).setTitle("Stroke width").setItems(arrayOf("2", "4", "6", "12", "20", "32")) { _, i ->
                canvas.strokeSize = listOf(2, 4, 6, 12, 20, 32)[i]
            }.show()
        })
        tools.addView(CheckBox(this).apply { text = "Filled"; setOnCheckedChangeListener { _, b -> canvas.filled = b } })
        tools.addView(button("Undo") { canvas.undo() }); tools.addView(button("Redo") { canvas.redo() })
        drawingToolbar = HorizontalScrollView(this).apply { addView(tools) }
        root.addView(drawingToolbar)
        val audio = row()
        micButton = button("Mic off") { toggleMic() }; audioButton = button("Audio on") { voice.listening = !voice.listening }
        socialButton = button("People") { communityUI.hub() }
        weighted(audio, micButton!!); weighted(audio, audioButton!!); weighted(audio, socialButton!!); root.addView(audio)
        root.addView(tabs(listOf("Chat", "Players", "Layers"), gameTab) { gameTab = it; lastList = ""; refreshList() })
        listArea = column(); root.addView(scroll(listArea!!), LinearLayout.LayoutParams(-1, 0, 1f))
        val compose = row()
        guessSwitch = Switch(this).apply { text = "Guess"; isChecked = true; setOnCheckedChangeListener { _, _ -> updateControls() } }
        messageInput = input("Your guess or message", limit = 300)
        sendButton = button("Send") {
            val edit = messageInput ?: return@button
            client.sendText(edit.text.toString(), guessSwitch?.isChecked == true); edit.text.clear()
        }
        compose.addView(guessSwitch)
        compose.addView(messageInput, LinearLayout.LayoutParams(0, -2, 1f)); compose.addView(sendButton)
        root.addView(compose)
    }
    private fun updateControls() {
        if (screen != "game") return
        roundLabel?.text = client.roundLabel
        wordLabel?.text = when {
            client.word.isNotEmpty() -> (if (client.canDraw) "Your word: " else "Answer: ") + client.word
            client.queued -> "Waiting for a player slot · position " + client.queuePosition
            client.spectator -> "Spectating"
            client.guessed -> "Correct! Watch the rest of the round."
            else -> "Watch the drawing and guess!"
        }
        val allowed = if (guessSwitch?.isChecked == true) client.canGuess else client.canTextChat
        sendButton?.isEnabled = allowed; messageInput?.isEnabled = allowed
        val roundKey = client.drawer + ":" + client.roundActive + ":" + client.scene.resetCount
        if (roundKey != lastRound || (lastDrawingAllowed && !client.canDraw)) board?.resetEditing()
        lastRound = roundKey; lastDrawingAllowed = client.canDraw
        if (!client.canSpeak) voice.stopRecording()
        if (!client.connected) voice.stopPlayback()
        updateVoiceControls(); board?.editingChanged?.invoke()
        drawingToolbar?.visibility = if (client.canDraw) View.VISIBLE else View.GONE
        board?.invalidate()
    }
    private fun render() {
        if (screen != client.page) { voice.close(); if (client.page == "login") communityUI.close(); buildScreen() }
        status.text = client.status
        status.visibility = if (screen == "game" && !client.busy && client.status == client.room.optString("roomName")) View.GONE else View.VISIBLE
        progress.visibility = if (client.busy) View.VISIBLE else View.GONE
        authButton?.isEnabled = !client.busy
        updateControls(); refreshList()
        val socialTitle = if (screen == "game") "People" else "People & messages"
        socialButton?.text = socialTitle + if (client.community.badge > 0) " (${client.community.badge})" else ""
        communityUI.refresh()
    }
    private fun refreshList() {
        val area = listArea ?: return
        val signature = when (screen) {
            "lobby" -> if (lobbyTab == 0) client.rooms.toString() else client.leaderboards.toString()
            "game" -> "$gameTab:" + when (gameTab) { 0 -> client.messages.toString(); 1 -> client.players.toString(); else -> client.scene.layers.toString() + client.canDraw }
            else -> return
        }
        if (signature == lastList) return
        lastList = signature; area.removeAllViews()
        if (screen == "lobby") {
            area.addView(button("Refresh") { client.refreshRooms(); client.refreshLeaderboards() })
            if (lobbyTab == 0) {
                if (client.rooms.length() == 0) area.addView(label("No rooms yet. Check the master server and refresh."))
                client.rooms.objects().forEach { room ->
                    val card = column().apply {
                        setPadding(dp(12), dp(10), dp(12), dp(10))
                        tag = "card"
                    }
                    card.addView(label(room.optString("roomName") + if (room.optBoolean("privateRoom")) " · Private" else "", 20f))
                    card.addView(label("${room.optInt("players")} / ${room.optInt("maxPlayers")} players · ${room.optInt("spectators")} watching", 14f))
                    val buttons = row()
                    fun join(spectator: Boolean) {
                        if (room.optBoolean("privateRoom")) prompt("Room passkey", true) { client.join(room, spectator, it) }
                        else client.join(room, spectator)
                    }
                    weighted(buttons, button("Play") { join(false) }); weighted(buttons, button("Watch") { join(true) })
                    card.addView(buttons); area.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
                }
            } else listOf("guessing", "drawing").forEach { track ->
                area.addView(label(track.replaceFirstChar { it.uppercase() }, 22f))
                (client.leaderboards[track] ?: JSONArray()).objects().take(100).forEach {
                    playerRow(area, it, "#${it.optInt(track + "LeaderboardRank")} · ${it.optInt(track + "Score")} points")
                }
            }
        } else when (gameTab) {
            0 -> {
                if (client.messages.isEmpty()) area.addView(label("Room messages will appear here."))
                client.messages.takeLast(80).forEach { area.addView(label(it, 14f)) }
                area.post { (area.parent as? ScrollView)?.fullScroll(View.FOCUS_DOWN) }
            }
            1 -> client.players.objects().forEach {
                playerRow(area, it.obj("profile"), (if (it.optBoolean("spectator")) "Spectator" else "Player") +
                    " · " + it.optInt("roomScore") + " points" + if (it.optBoolean("guessed")) " · Guessed" else "")
            }
            else -> layers(area)
        }
        appearance.apply(area)
    }
    private fun playerRow(area: LinearLayout, p: JSONObject, detail: String) {
        val role = if (p.optBoolean("masterSuperAdmin")) "Super-admin" else p.optString("serverRole", "player")
        communityUI.playerCard(area, p, "$detail · $role")
    }
    private fun layers(area: LinearLayout) {
        val canvas = board ?: return
        area.addView(label("Layers · bottom to top", 14f))
        client.scene.layers.forEach { layer ->
            area.addView(button(layer.optString("name") + (if (layer.optBoolean("locked")) " · Locked" else "") +
                if (!layer.optBoolean("visible", true)) " · Hidden" else "") {
                canvas.selectedLayer = layer.optString("id")
                val actions = arrayOf("Select", "Rename", "Show / hide", "Lock / unlock", "Move up (under)", "Move down (over)", "Clear layer", "Delete layer")
                AlertDialog.Builder(this).setTitle(layer.optString("name")).setItems(actions) { _, which ->
                    if (!client.canDraw || which == 0) return@setItems
                    val id = layer.optString("id")
                    fun update(key: String, value: Any) { client.operation(json("action" to "layer_upsert", "layer" to layer.copy().put(key, value))) }
                    when (which) {
                        1 -> prompt("Layer name", false) { update("name", it.take(32)) }
                        2 -> update("visible", !layer.optBoolean("visible", true))
                        3 -> update("locked", !layer.optBoolean("locked"))
                        4, 5 -> {
                            val ids = client.scene.layers.map { it.optString("id") }.toMutableList()
                            val index = ids.indexOf(id); val target = index + if (which == 4) -1 else 1
                            if (target in ids.indices) { java.util.Collections.swap(ids, index, target); client.operation(json("action" to "layer_reorder", "layerIds" to JSONArray(ids))) }
                        }
                        6 -> client.operation(json("action" to "clear_layer", "layerId" to id))
                        7 -> client.operation(json("action" to "layer_delete", "layerId" to id))
                    }
                    canvas.resetEditing()
                }.show()
            })
        }
        area.addView(button("Add layer") {
            if (client.canDraw && client.scene.layers.size < 32) prompt("Layer name", false) {
                val id = java.util.UUID.randomUUID().toString()
                client.operation(json("action" to "layer_upsert", "layer" to json("id" to id,
                    "name" to it.take(32), "order" to client.scene.layers.size, "visible" to true, "locked" to false)))
                canvas.selectedLayer = id
            }
        })
    }
    private fun prompt(title: String, secret: Boolean, action: (String) -> Unit) {
        val edit = input(title, password = secret)
        AlertDialog.Builder(this).setTitle(title).setView(edit).setNegativeButton("Cancel", null)
            .setPositiveButton("Continue") { _, _ -> if (edit.text.isNotBlank()) action(edit.text.toString()) }.show()
    }
    private fun roomOptions(anchor: View) {
        PopupMenu(this, anchor).apply {
            menu.add("Find players").setOnMenuItemClickListener { communityUI.hub("Find players"); true }
            menu.add("Friends & inbox").setOnMenuItemClickListener { communityUI.hub(); true }
            if (client.community.canModerate) menu.add("Server moderation").setOnMenuItemClickListener { communityUI.hub("Moderation"); true }
            val role = if (client.queued) "Cancel player queue" else if (client.spectator) "Join as player" else "Switch to spectator"
            menu.add(role).setOnMenuItemClickListener { client.changeRole(); true }
            menu.add(if (voice.recording) "Turn microphone off" else "Turn microphone on").setOnMenuItemClickListener {
                if (voice.recording) voice.stopRecording()
                else if (client.canSpeak) {
                    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) enableMic()
                    else microphonePermission.launch(Manifest.permission.RECORD_AUDIO)
                } else client.notice("Drawing and correctly guessing players can only listen this round.")
                true
            }
            menu.add(if (voice.listening) "Mute room audio" else "Listen to room audio").setOnMenuItemClickListener {
                voice.listening = !voice.listening; if (!voice.listening) voice.stopPlayback(); true
            }
            menu.add("Export drawing").setOnMenuItemClickListener {
                exportBitmap?.recycle(); exportBitmap = board?.snapshot()
                exportDrawing.launch("drawing.png"); true
            }
            menu.add("Clear canvas").setOnMenuItemClickListener {
                if (client.canDraw) AlertDialog.Builder(this@MainActivity).setMessage("Clear all canvas objects?")
                    .setNegativeButton("Cancel", null).setPositiveButton("Clear") { _, _ ->
                        client.operation(json("action" to "clear")); board?.resetEditing()
                    }.show()
                true
            }
            menu.add("Leave room").setOnMenuItemClickListener { confirmLeave(); true }
        }.show()
    }
    private fun accountOptions(anchor: View) {
        PopupMenu(this, anchor).apply {
            menu.add("Sign out").setOnMenuItemClickListener { client.logout(); true }
        }.show()
    }
    private fun enableMic() { if (voice.start()) client.notice("Microphone on") }
    private fun toggleMic() {
        if (voice.recording) voice.stopRecording()
        else if (client.canSpeak) {
            if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) enableMic()
            else microphonePermission.launch(Manifest.permission.RECORD_AUDIO)
        } else client.notice("Drawing and correctly guessing players cannot speak this round.")
    }
    private fun updateVoiceControls() {
        micButton?.text = if (voice.recording) "● Mic on" else "Mic off"
        micButton?.isEnabled = client.canSpeak || voice.recording
        audioButton?.text = if (voice.listening) "Audio on" else "Audio muted"
    }
    internal fun chooseTheme() {
        val ids = listOf("system") + ThemePalette.all.map { it.id }
        val names = listOf("System") + ThemePalette.all.map { it.name }
        AlertDialog.Builder(this).setTitle("Appearance").setSingleChoiceItems(names.toTypedArray(), ids.indexOf(appearance.selection)) { dialog, which ->
            appearance.select(ids[which]); root.setBackgroundColor(appearance.palette.window); appearance.apply(root)
            dialog.dismiss()
        }.setNegativeButton("Close", null).show()
    }
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig); appearance.configure()
        root.setBackgroundColor(appearance.palette.window); appearance.apply(root)
    }
    private fun confirmLeave() {
        AlertDialog.Builder(this).setMessage("Leave this room?").setNegativeButton("Stay", null)
            .setPositiveButton("Leave") { _, _ -> voice.close(); client.leaveRoom() }.show()
    }
    override fun onStart() { super.onStart(); voice.foreground = true; client.setForeground(true) }
    override fun onStop() { voice.foreground = false; board?.cancelCurve(); client.setForeground(false); super.onStop() }
    override fun onDestroy() { client.changed = {}; client.voiceReceived = {}; voice.changed = {}; communityUI.close(); voice.close(); client.close(); exportBitmap?.recycle(); super.onDestroy() }
}
