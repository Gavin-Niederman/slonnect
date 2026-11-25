/*
 * SPDX-FileCopyrightText: 2026 Slonnect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */
package dev.slonnect.mobile.bridge

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper

/** A conversation the bridge has seen messages for. */
data class BridgeConversation(
    val threadId: Long,
    /** Stable key from the notification (shortcut id, else title). */
    val key: String,
    val title: String,
    /** Phone numbers / emails of the other participants; display names when unknown. */
    val addresses: List<String>,
    val isGroup: Boolean,
    val lastRcsAt: Long,
)

data class BridgeMessage(
    val uid: Long,
    val threadId: Long,
    val body: String,
    val date: Long,
    val outgoing: Boolean,
    /** Sender's address or name in a group; null for outgoing or 1:1. */
    val sender: String?,
    val transport: String,
)

/**
 * Local copy of the messages that only reached the phone through notifications (RCS and other
 * chats the SMS provider never sees), so the desktop can page through them like SMS. Lives in
 * the app's private storage and never leaves the phone except to the paired desktop.
 */
class BridgeStore(context: Context, name: String = "slonnect_bridge.db") :
    SQLiteOpenHelper(context, name, null, 1) {

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE conversations (
                thread_id INTEGER PRIMARY KEY, key TEXT UNIQUE NOT NULL, title TEXT NOT NULL,
                addresses TEXT NOT NULL, is_group INTEGER NOT NULL, last_rcs_at INTEGER NOT NULL)"""
        )
        db.execSQL(
            """CREATE TABLE messages (
                uid INTEGER PRIMARY KEY AUTOINCREMENT, thread_id INTEGER NOT NULL, body TEXT NOT NULL,
                date INTEGER NOT NULL, outgoing INTEGER NOT NULL, sender TEXT, transport TEXT NOT NULL,
                dedup TEXT UNIQUE NOT NULL)"""
        )
        db.execSQL("CREATE INDEX messages_thread_date ON messages(thread_id, date)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {}

    @Synchronized
    fun conversationByKey(key: String): BridgeConversation? = conversations("key = ?", arrayOf(key)).firstOrNull()

    @Synchronized
    fun conversation(threadId: Long): BridgeConversation? = conversations("thread_id = ?", arrayOf(threadId.toString())).firstOrNull()

    @Synchronized
    fun allConversations(): List<BridgeConversation> = conversations(null, null)

    private fun conversations(where: String?, args: Array<String>?): List<BridgeConversation> =
        readableDatabase.query("conversations", null, where, args, null, null, "last_rcs_at DESC").use { c ->
            buildList {
                while (c.moveToNext()) add(
                    BridgeConversation(
                        c.getLong(0), c.getString(1), c.getString(2),
                        c.getString(3).split('\u001f').filter { it.isNotEmpty() }, c.getInt(4) != 0, c.getLong(5),
                    )
                )
            }
        }

    @Synchronized
    fun upsertConversation(conv: BridgeConversation) {
        val v = ContentValues().apply {
            put("thread_id", conv.threadId)
            put("key", conv.key)
            put("title", conv.title)
            put("addresses", conv.addresses.joinToString("\u001f"))
            put("is_group", if (conv.isGroup) 1 else 0)
            put("last_rcs_at", conv.lastRcsAt)
        }
        writableDatabase.insertWithOnConflict("conversations", null, v, SQLiteDatabase.CONFLICT_REPLACE)
    }

    /** Next free thread id for a conversation without a telephony thread (≥ 2^40, see protocol/README.md). */
    @Synchronized
    fun newSyntheticThreadId(): Long {
        val max = readableDatabase.rawQuery("SELECT MAX(thread_id) FROM conversations WHERE thread_id >= ?", arrayOf(SYNTHETIC_BASE.toString()))
            .use { if (it.moveToFirst() && !it.isNull(0)) it.getLong(0) else SYNTHETIC_BASE - 1 }
        return max + 1
    }

    /** Inserts unless an identical message was stored already; returns it with its uid, or null if it was a duplicate. */
    @Synchronized
    fun insert(m: BridgeMessage, dedup: String): BridgeMessage? {
        val v = ContentValues().apply {
            put("thread_id", m.threadId)
            put("body", m.body)
            put("date", m.date)
            put("outgoing", if (m.outgoing) 1 else 0)
            put("sender", m.sender)
            put("transport", m.transport)
            put("dedup", dedup)
        }
        val id = writableDatabase.insertWithOnConflict("messages", null, v, SQLiteDatabase.CONFLICT_IGNORE)
        return if (id == -1L) null else m.copy(uid = id)
    }

    /** Is there a message in the thread with this body within ±windowMs of date? */
    @Synchronized
    fun hasSimilar(threadId: Long, body: String, date: Long, windowMs: Long): Boolean =
        readableDatabase.rawQuery(
            "SELECT 1 FROM messages WHERE thread_id = ? AND body = ? AND date BETWEEN ? AND ? LIMIT 1",
            arrayOf(threadId.toString(), body, (date - windowMs).toString(), (date + windowMs).toString()),
        ).use { it.moveToFirst() }

    /** Messages of a thread, newest first; only those older than [before] if given. */
    @Synchronized
    fun messages(threadId: Long?, before: Long? = null, limit: Long? = null): List<BridgeMessage> {
        val where = mutableListOf<String>()
        val args = mutableListOf<String>()
        threadId?.let { where += "thread_id = ?"; args += it.toString() }
        before?.let { where += "date < ?"; args += it.toString() }
        return readableDatabase.query(
            "messages", arrayOf("uid", "thread_id", "body", "date", "outgoing", "sender", "transport"),
            where.joinToString(" AND ").ifEmpty { null }, args.toTypedArray().ifEmpty { null },
            null, null, "date DESC", limit?.toString(),
        ).use { c ->
            buildList {
                while (c.moveToNext()) add(
                    BridgeMessage(c.getLong(0), c.getLong(1), c.getString(2), c.getLong(3), c.getInt(4) != 0,
                        if (c.isNull(5)) null else c.getString(5), c.getString(6))
                )
            }
        }
    }

    @Synchronized
    fun newest(threadId: Long): BridgeMessage? = messages(threadId, limit = 1).firstOrNull()

    @Synchronized
    fun clear() {
        writableDatabase.delete("messages", null, null)
        writableDatabase.delete("conversations", null, null)
    }

    companion object {
        const val SYNTHETIC_BASE = 1L shl 40
        /** Bridged message uids are offset so they never collide with the SMS provider's _id. */
        const val UID_BASE = 1L shl 40
    }
}
