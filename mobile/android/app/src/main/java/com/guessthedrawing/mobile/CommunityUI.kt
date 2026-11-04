package com.guessthedrawing.mobile

import android.app.AlertDialog
import android.app.DatePickerDialog
import android.app.TimePickerDialog
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import org.json.JSONObject
import java.text.DateFormat
import java.util.Calendar
import java.util.Date

/** Native, touch-sized sheets stay on the game activity so opening a profile never drops the room. */
class CommunityUI(private val activity: MainActivity) {
    private val client get() = activity.client
    private val social get() = client.community
    private val watchers = mutableMapOf<AlertDialog, () -> Unit>()
    fun refresh() { watchers.toMap().forEach { (dialog, draw) -> if (dialog.isShowing) draw() } }
    fun close() { watchers.keys.toList().forEach { it.dismiss() }; watchers.clear() }
    private fun date(seconds: Long) = if (seconds <= 0) L10n.text("Permanent") else DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT, L10n.locale).format(Date(seconds * 1000))
    private fun label(text: String, size: Float = 15f, translate: Boolean = true) = activity.label(text, size, translate)
    private fun button(text: String, action: () -> Unit) = activity.button(text, action = action)
    private fun column() = activity.column()
    private fun card(parent: LinearLayout, title: String, detail: String = "", rawTitle: Boolean = false, rawDetail: Boolean = false, action: (() -> Unit)? = null): LinearLayout {
        val box = column().apply { tag = "card"; setPadding(activity.dp(12), activity.dp(8), activity.dp(12), activity.dp(8)) }
        box.addView(label(title, 17f, translate = !rawTitle).apply { setTypeface(typeface, Typeface.BOLD) })
        if (detail.isNotEmpty()) box.addView(label(detail, 13f, translate = !rawDetail))
        if (action != null) { box.isClickable = true; box.isFocusable = true; box.setOnClickListener { action() } }
        parent.addView(box, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = activity.dp(10) })
        return box
    }
    private data class Sheet(val dialog: AlertDialog, val body: LinearLayout, val content: LinearLayout)
    private fun sheet(title: String): Sheet {
        val body = column().apply { setPadding(activity.dp(14), activity.dp(8), activity.dp(14), activity.dp(12)) }
        val header = activity.row()
        header.addView(label(title, 23f).apply { setTypeface(typeface, Typeface.BOLD) }, LinearLayout.LayoutParams(0, -2, 1f))
        val dialog = LocalizedDialogBuilder(activity).setView(body).create()
        header.addView(button("Close") { dialog.dismiss() }); body.addView(header)
        val progress = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { isIndeterminate = true; tag = "pending" }
        body.addView(progress, LinearLayout.LayoutParams(-1, activity.dp(3)))
        body.addView(label("").apply { tag = "status" })
        val content = column()
        body.addView(activity.scroll(content), LinearLayout.LayoutParams(-1, 0, 1f))
        dialog.setOnDismissListener { watchers.remove(dialog) }
        dialog.show(); dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, (activity.resources.displayMetrics.heightPixels * 0.9).toInt())
        dialog.window?.setBackgroundDrawable(android.graphics.drawable.ColorDrawable(activity.appearance.palette.window))
        body.setBackgroundColor(activity.appearance.palette.window)
        return Sheet(dialog, body, content)
    }
    private fun watch(sheet: Sheet, signature: () -> String, draw: (LinearLayout) -> Unit) {
        var previous = ""
        val redraw = {
            sheet.body.findViewWithTag<ProgressBar>("pending").visibility = if (social.busy) View.VISIBLE else View.GONE
            sheet.body.findViewWithTag<TextView>("status").apply { text = L10n.text(social.status); visibility = if (text.isEmpty()) View.GONE else View.VISIBLE }
            val key = signature()
            if (key != previous) { previous = key; sheet.content.removeAllViews(); draw(sheet.content) }
            activity.appearance.apply(sheet.body)
        }
        watchers[sheet.dialog] = redraw; redraw()
    }
    fun hub(initial: String = "Friends") {
        val sheet = sheet("People & messages")
        val sections = listOf("Friends", "Requests", "Inbox", "Notifications", "Find players", "Blocked") + if (social.canModerate) listOf("Moderation") else emptyList()
        var section = initial.takeIf { it in sections } ?: "Friends"
        val chooser = Spinner(activity).apply { adapter = L10n.options(activity, sections); setSelection(sections.indexOf(section)) }
        sheet.body.addView(chooser, 3)
        val searchRow = activity.row()
        val search = activity.input("Username or display name", social.query, limit = 80)
        searchRow.addView(search, LinearLayout.LayoutParams(0, -2, 1f)); searchRow.addView(button("Search") { social.search(search.text.toString()) })
        sheet.body.addView(searchRow, 4)
        chooser.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                section = sections[position]; searchRow.visibility = if (section == "Find players") View.VISIBLE else View.GONE
                if (section == "Moderation") social.refreshAdmin()
                refresh()
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
        watch(sheet, { section + social.friends + social.requests + social.conversations + social.notifications + social.blocked + social.users + social.admin + social.invitations + social.canModerate }) { area ->
            when (section) {
                "Friends", "Find players" -> {
                    val entries = if (section == "Friends") social.friends else social.users
                    if (entries.isEmpty()) area.addView(label(if (section == "Friends") "Find players to send your first friend request." else "Search by username or display name."))
                    entries.forEach { entry -> playerCard(area, entry.obj("profile"), L10n.text(entry.optString("presenceStatus")) + " · " + L10n.text(entry.optString("relationship"))) }
                }
                "Requests" -> {
                    social.requests.forEach { request ->
                        val outgoing = request.optString("senderUsername") == client.username
                        val user = request.optString(if (outgoing) "recipientUsername" else "senderUsername")
                        val state = request.optString("status")
                        val box = card(area, L10n.format(if (outgoing) "To @%1" else "From @%1", user), state) { profile(user) }
                        if (!outgoing && state == "pending") {
                            box.addView(button("Accept") { social.action("FriendRequestAccept", fields = json("friendRequestId" to request.optLong("id"))) })
                            box.addView(button("Decline") { social.action("FriendRequestDeny", fields = json("friendRequestId" to request.optLong("id"))) })
                        } else if (state == "denied") box.addView(button("Clear declined request") {
                            social.action("FriendRequestClear", fields = json("friendRequestId" to request.optLong("id")))
                            social.action("FriendRequestNotificationClear", user)
                        })
                    }
                    social.notifications.filter { it.optString("kind") == "friend_request_accepted" }.forEach { notificationCard(area, it) }
                    if (area.childCount == 0) area.addView(label("No friend requests. You're all caught up."))
                }
                "Inbox" -> {
                    social.conversations.forEach { item ->
                        val user = item.optString("username")
                        val box = playerCard(area, item, L10n.format("%1 unread · %2", item.optInt("unreadCount"), L10n.text(item.optString("presence"))))
                        box.setOnClickListener { inbox(user) }
                        box.addView(button("Open conversation") { inbox(user) })
                        box.addView(button("Delete conversation") { confirm("Delete your copy of this conversation?") { social.action("InboxConversationDelete", user) } })
                    }
                    if (area.childCount == 0) area.addView(label("Your private conversations with friends appear here."))
                }
                "Notifications" -> {
                    social.invitations.toList().forEach { invite ->
                        val room = invite.obj("room")
                        val box = card(area, "Invitation from @${invite.optString("fromUsername")}", room.optString("roomName"), rawDetail = true)
                        box.addView(button("Play") { join(room, false); social.dismissInvitation(invite) })
                        box.addView(button("Watch") { join(room, true); social.dismissInvitation(invite) })
                        box.addView(button("Dismiss") { social.dismissInvitation(invite) })
                    }
                    social.notifications.forEach { notificationCard(area, it) }
                    if (area.childCount == 0) area.addView(label("No new notifications. You're all caught up."))
                }
                "Blocked" -> {
                    social.blocked.forEach { user -> card(area, "@$user", "Blocked") { profile(user) }.addView(button("Unblock") { social.action("BlockSet", user, json("blocked" to false)) }) }
                    if (area.childCount == 0) area.addView(label("You haven't blocked anyone."))
                }
                "Moderation" -> moderationList(area)
            }
            area.addView(button("Refresh") { social.refresh(); if (section == "Moderation") social.refreshAdmin(); if (section == "Find players") social.search(search.text.toString()) })
        }
        searchRow.visibility = if (section == "Find players") View.VISIBLE else View.GONE
        social.refresh(); if (section == "Moderation") social.refreshAdmin()
    }
    private fun notificationCard(area: LinearLayout, notification: JSONObject) {
        val user = notification.optString("relatedUsername")
        val message = when (notification.optString("kind")) { "friend_request_accepted" -> "@$user accepted your friend request"; "friends_now" -> "You and @$user are now friends"; else -> "@$user declined your friend request" }
        card(area, message, "${date(notification.optLong("createdAt"))} · Tap to clear") {
            social.action("NotificationClear", fields = json("notificationId" to notification.optLong("id")))
        }
    }
    fun playerCard(area: LinearLayout, profile: JSONObject, detail: String): LinearLayout {
        val user = profile.optString("username")
        val box = column().apply { tag = "card"; setPadding(activity.dp(8), activity.dp(8), activity.dp(8), activity.dp(8)) }
        val identity = activity.row()
        identity.addView(label(profile.optString("displayName", user).take(1).uppercase(), 25f, translate = false).apply {
            gravity = Gravity.CENTER; setTypeface(typeface, Typeface.BOLD)
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(activity.appearance.palette.button) }
        }, LinearLayout.LayoutParams(activity.dp(48), activity.dp(48)))
        val country = profile.optString("countryCode")
        if (country.isNotEmpty()) runCatching {
            val image = ImageView(activity)
            activity.assets.open("country-flags/${country.lowercase()}.png").use { image.setImageDrawable(android.graphics.drawable.Drawable.createFromStream(it, null)) }
            image.contentDescription = L10n.text(country); identity.addView(image, LinearLayout.LayoutParams(activity.dp(28), activity.dp(22)))
        }
        identity.addView(label(L10n.isolate(profile.optString("displayName", user)) + "\n" + L10n.isolate("@$user") + (if (country.isEmpty()) "" else " · " + L10n.isolate(country)), 16f, translate = false), LinearLayout.LayoutParams(0, -2, 1f))
        box.addView(identity); if (detail.isNotBlank()) box.addView(label(L10n.detail(detail), 13f, translate = false))
        box.isClickable = true; box.isFocusable = true; box.setOnClickListener { profile(user) }
        box.setOnLongClickListener { contextMenu(box, user); true }
        area.addView(box, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = activity.dp(10) })
        activity.appearance.apply(box); return box
    }
    fun profile(user: String) {
        val sheet = sheet("Player profile")
        watch(sheet, { social.profiles[user].toString() + social.targetProfile(user) + social.mutedText + social.mutedVoice + social.level + client.connected }) { area ->
            val entry = social.profiles[user]
            val p = social.targetProfile(user)
            playerCard(area, p, "${entry?.optString("presenceStatus", "Loading…") ?: "Loading…"} · ${p.optString("serverRole", "player")}").apply { setOnClickListener(null); setOnLongClickListener(null) }
            val scores = card(area, "Game stats")
            scores.addView(label(L10n.format("Total score  %1\nGuessing  %2 · %3\nDrawing  %4 · %5", p.optInt("totalScore"),
                p.optInt("guessingScore"), L10n.text(p.optString("guessingRank")), p.optInt("drawingScore"), L10n.text(p.optString("drawingRank")))))
            scores.addView(label(L10n.format("Wins  %1    Losses  %2\nAbandoned  %3", p.optInt("roundsWon"), p.optInt("roundsLost"), p.optInt("roundsAbandoned"))))
            scores.addView(label(L10n.format("Leaderboard  #%1\nGuessing rank  #%2\nDrawing rank  #%3", p.optInt("leaderboardRank"), p.optInt("guessingLeaderboardRank"), p.optInt("drawingLeaderboardRank"))))
            card(area, "Account", L10n.format("Relationship: %1\nRole: %2", L10n.text(entry?.optString("relationship", "Loading…") ?: "Loading…"), L10n.text(if (p.optBoolean("masterSuperAdmin")) "Master super-admin" else p.optString("serverRole", "player"))))
            if (p.optString("avatarUrl").isNotEmpty()) area.addView(label("Avatar: ${p.optString("avatarUrl")}", 12f))
            if (entry?.optBoolean("hasRoom") == true) card(area, "Current room", entry.obj("room").optString("roomName"), rawDetail = true)
            entry?.array("activeBans")?.objects()?.forEach { ban -> card(area, "Active ban · ${ban.optString("serverId")}", "${ban.optString("reason")}\n${date(ban.optLong("expiresAt"))}", rawDetail = true) }
            if (entry != null && entry.has("relationship")) {
                area.addView(label("Player actions", 21f))
                actions(user).chunked(2).forEach { pair ->
                    val row = activity.row()
                    pair.forEach { (name, action) -> row.addView(button(name, action), LinearLayout.LayoutParams(0, -2, 1f).apply {
                        setMargins(activity.dp(3), activity.dp(4), activity.dp(3), activity.dp(4))
                    }) }
                    area.addView(row)
                }
            }
            else area.addView(label("Loading full profile…"))
        }
        social.profile(user)
    }
    fun contextMenu(anchor: View, user: String) {
        // Load the full relationship before showing its context actions.
        if (social.profiles[user]?.has("relationship") != true) { profile(user); return }
        PopupMenu(activity, anchor).apply {
            menu.add(L10n.text("View profile")).setOnMenuItemClickListener { profile(user); true }
            actions(user).forEach { (label, action) -> menu.add(L10n.text(label)).setOnMenuItemClickListener { action(); true } }
        }.show()
    }
    private fun actions(user: String): List<Pair<String, () -> Unit>> {
        if (user == client.username) return emptyList()
        val entry = social.profiles[user] ?: JSONObject()
        val result = mutableListOf<Pair<String, () -> Unit>>()
        fun add(name: String, action: () -> Unit) { result.add(name to action) }
        when (entry.optString("relationship", "none")) {
            "none" -> add("Send friend request") { social.action("FriendRequestSend", user) }
            "incoming" -> {
                add("Accept friend request") { social.action("FriendRequestAccept", fields = json("friendRequestId" to entry.optLong("relationshipId"))) }
                add("Decline friend request") { social.action("FriendRequestDeny", fields = json("friendRequestId" to entry.optLong("relationshipId"))) }
            }
            "denied" -> add("Clear declined request") { social.action("FriendRequestClear", fields = json("friendRequestId" to entry.optLong("relationshipId"))); social.action("FriendRequestNotificationClear", user) }
            "friend" -> {
                add("Message") { inbox(user) }
                if (client.connected) add("Invite to my room") { social.action("RoomInviteSend", user) }
                add("Remove friend") { confirm("Remove @$user from your friends?") { social.action("FriendRemove", user) } }
            }
        }
        val blocked = entry.optString("relationship") == "blocked"
        add(if (blocked) "Unblock user" else "Block user") { confirm(if (blocked) "Unblock @$user?" else "Block @$user?") { social.action("BlockSet", user, json("blocked" to !blocked)) } }
        if (entry.optBoolean("hasRoom")) {
            add("Join player's room") { join(entry.obj("room"), false) }; add("Watch player's room") { join(entry.obj("room"), true) }
        }
        val target = social.targetProfile(user)
        val targetLevel = CommunityPolicy.level(target)
        val inRoster = client.players.objects().any { it.obj("profile").optString("username") == user }
        if (client.connected && targetLevel == 0 && (inRoster || target.optBoolean("roomRoleLoaded"))) {
            add(if (user in social.mutedText) "Unmute text" else "Mute text") { social.mute(user, user !in social.mutedText, user in social.mutedVoice) }
            add(if (user in social.mutedVoice) "Unmute voice" else "Mute voice") { social.mute(user, user in social.mutedText, user !in social.mutedVoice) }
            if (inRoster) add("Report player") { reason("Report @$user") { social.roomRequest("PlayerReport", user, json("reason" to it)) } }
        }
        if (social.canModerate && social.level > targetLevel && (inRoster || target.optBoolean("roomRoleLoaded"))) {
            add("Kick player…") { moderation(user, "kick") }; add("Ban player…") { moderation(user, "ban") }; add("Server mute…") { moderation(user, "mute") }
            if (social.level >= 2) add("Set server role…") { setRole(user) }
        }
        return result
    }
    fun inbox(user: String) {
        val sheet = sheet("Inbox · @$user")
        social.openConversation = user
        val compose = activity.row(); val text = activity.input("Private message", limit = 2000)
        compose.addView(text, LinearLayout.LayoutParams(0, -2, 1f))
        compose.addView(button("Send") { if (text.text.isNotBlank() && social.action("DirectMessageSend", user, json("body" to text.text.toString().trim()))) text.text.clear() })
        sheet.body.addView(compose)
        sheet.dialog.setOnDismissListener { watchers.remove(sheet.dialog); if (social.openConversation == user) social.openConversation = "" }
        watch(sheet, { social.messages[user].toString() + social.hasOlder[user] }) { area ->
            if (social.hasOlder[user] == true) area.addView(button("Load older messages") { social.inbox(user, true) })
            social.messages[user].orEmpty().forEach { message ->
                val box = card(area, message.optString("body"), "${L10n.isolate("@" + message.optString("senderUsername"))} · ${date(message.optLong("createdAt"))}", rawTitle = true, rawDetail = true)
                box.setOnLongClickListener { PopupMenu(activity, box).apply {
                    menu.add(L10n.text("Delete message")).setOnMenuItemClickListener { confirm("Delete this message from your view?") { social.action("InboxMessageDelete", fields = json("messageId" to message.optLong("id"))) }; true }
                }.show(); true }
            }
            if (area.childCount == 0) area.addView(label("Say hello to @$user."))
        }
        social.inbox(user)
    }
    private fun join(room: JSONObject, spectator: Boolean) {
        fun proceed() {
            if (room.optBoolean("privateRoom")) {
                val passkey = activity.input("Room passkey", password = true)
                LocalizedDialogBuilder(activity).setTitle("Private room").setView(passkey).setNegativeButton("Cancel", null)
                    .setPositiveButton("Join") { _, _ -> close(); client.join(room, spectator, passkey.text.toString()) }.show()
            } else { close(); client.join(room, spectator) }
        }
        if (client.connected) confirm("Leave your current room and join ${room.optString("roomName")}?") { proceed() } else proceed()
    }
    private fun confirm(message: String, action: () -> Unit) {
        LocalizedDialogBuilder(activity).setMessage(message).setNegativeButton("Cancel", null).setPositiveButton("Confirm") { _, _ -> action() }.show()
    }
    private fun reason(title: String, action: (String) -> Unit) {
        val input = activity.input("Reason", limit = 500)
        val dialog = LocalizedDialogBuilder(activity).setTitle(title).setView(input).setNegativeButton("Cancel", null).setPositiveButton("Submit", null).create()
        dialog.setOnShowListener { dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            if (input.text.isBlank()) input.error = L10n.text("Enter a reason") else { action(input.text.toString().trim()); dialog.dismiss() }
        } }; dialog.show()
    }
    private fun moderationList(area: LinearLayout) {
        if (!social.canModerate) { area.addView(label("Your current role cannot manage this server.")); return }
        listOf("bans", "mutes").forEach { kind ->
            area.addView(label(kind.replaceFirstChar { it.uppercase() }, 21f))
            social.admin.array(kind).objects().forEach { item ->
                val user = item.optString("targetUsername")
                val channels = if (kind == "mutes") L10n.text(if (item.optBoolean("textChat") && item.optBoolean("voiceChat")) "Text and voice" else if (item.optBoolean("textChat")) "Text chat" else "Voice chat") + "\n" else ""
                val box = card(area, "@$user", channels + item.optString("reason") + "\n" + L10n.format("By @%1 · %2", item.optString("issuedBy"), date(item.optLong("expiresAt"))), rawDetail = true) { profile(user) }
                box.addView(button(if (kind == "bans") "Unban" else "Unmute") { confirm("Revoke this ${if (kind == "bans") "ban" else "mute"} for @$user?") {
                    social.roomRequest("ModerationRequest", user, json("action" to if (kind == "bans") "unban" else "unmute", "sanctionId" to item.optLong("id")))
                } })
            }
        }
        area.addView(label("Server staff", 21f))
        social.admin.array("staff").objects().forEach { item -> card(area, "@${item.optString("username")}", L10n.format("%1 · assigned by @%2", L10n.text(item.optString("role")), item.optString("assignedBy"))) { profile(item.optString("username")) } }
        if (social.level >= 2) area.addView(button("Assign staff role") { setRole("") })
    }
    private fun setRole(user: String) {
        val body = column(); val username = activity.input("Account username", user, limit = 24)
        val roles = if (social.level >= 3) listOf("player", "moderator", "admin") else listOf("player", "moderator")
        val role = Spinner(activity).apply { adapter = L10n.options(activity, roles) }
        body.addView(username); body.addView(role)
        LocalizedDialogBuilder(activity).setTitle("Server role").setView(body).setNegativeButton("Cancel", null).setPositiveButton("Set role") { _, _ ->
            if (username.text.isNotBlank()) social.roomRequest("StaffRoleRequest", username.text.toString().trim(), json("role" to role.selectedItem.toString()))
        }.show()
    }
    private fun moderation(user: String, action: String) {
        if (action == "kick") { reason("Kick @$user") { social.roomRequest("ModerationRequest", user, json("action" to action, "reason" to it)) }; return }
        val sheet = sheet("${action.replaceFirstChar { it.uppercase() }} @$user")
        val area = sheet.content; val reason = activity.input("Reason", limit = 500); area.addView(reason)
        val text = CheckBox(activity).apply { this.text = L10n.text("Text chat"); isChecked = true }
        val voice = CheckBox(activity).apply { this.text = L10n.text("Voice chat"); isChecked = true }
        if (action == "mute") { area.addView(text); area.addView(voice) }
        val modes = listOf("For a duration", "Until date and time", "Permanent")
        val mode = Spinner(activity).apply { adapter = L10n.options(activity, modes) }
        val amount = activity.input("Duration amount", "1", limit = 6).apply { inputType = InputType.TYPE_CLASS_NUMBER }
        val units = Spinner(activity).apply { adapter = L10n.options(activity, CommunityPolicy.durationUnits.keys.toList()); setSelection(2) }
        val until = Calendar.getInstance().apply { add(Calendar.DAY_OF_YEAR, 1) }
        lateinit var dateButton: Button
        dateButton = button(date(until.timeInMillis / 1000)) {
            DatePickerDialog(activity, { _, y, m, d ->
                until.set(y, m, d)
                TimePickerDialog(activity, { _, h, min ->
                    until.set(Calendar.HOUR_OF_DAY, h); until.set(Calendar.MINUTE, min); until.set(Calendar.SECOND, 0)
                    dateButton.text = L10n.text(date(until.timeInMillis / 1000))
                }, until.get(Calendar.HOUR_OF_DAY), until.get(Calendar.MINUTE), true).show()
            }, until.get(Calendar.YEAR), until.get(Calendar.MONTH), until.get(Calendar.DAY_OF_MONTH)).show()
        }
        listOf(mode, amount, units, dateButton).forEach { area.addView(it) }
        mode.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, i: Int, id: Long) { amount.visibility = if (i == 0) View.VISIBLE else View.GONE; units.visibility = amount.visibility; dateButton.visibility = if (i == 1) View.VISIBLE else View.GONE }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val error = label(""); area.addView(error)
        area.addView(button(if (action == "mute") "Apply mute" else "Apply ban") {
            val duration = L10n.digits(amount.text.toString()).toLongOrNull() ?: 0
            val expiry = when (mode.selectedItemPosition) { 0 -> System.currentTimeMillis() / 1000 + duration * CommunityPolicy.durationUnits.values.elementAt(units.selectedItemPosition); 1 -> until.timeInMillis / 1000; else -> 0 }
            when {
                reason.text.isBlank() -> error.text = L10n.text("Enter a reason.")
                action == "mute" && !text.isChecked && !voice.isChecked -> error.text = L10n.text("Select text chat, voice chat, or both.")
                mode.selectedItemPosition == 0 && duration !in 1..100000 -> error.text = L10n.text("Choose an amount between 1 and 100000.")
                mode.selectedItemPosition != 2 && expiry <= System.currentTimeMillis() / 1000 -> error.text = L10n.text("Choose a future expiry.")
                else -> {
                    val fields = json("action" to action, "reason" to reason.text.toString().trim(), "textChat" to text.isChecked, "voiceChat" to voice.isChecked)
                    if (mode.selectedItemPosition != 2) fields.put("expiresAt", expiry)
                    if (social.roomRequest("ModerationRequest", user, fields)) sheet.dialog.dismiss()
                }
            }
        })
        activity.appearance.apply(sheet.body)
    }
}
