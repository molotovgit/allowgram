	case mtpc_messages_setTyping:
		return ReadPeerAllowed<MTPmessages_SetTyping>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_sendMessage:
		return ReadPeerAllowed<MTPmessages_SendMessage>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true, .replyFlag = (1U << 0) });
	case mtpc_messages_sendMedia:
		return ReadPeerAllowed<MTPmessages_SendMedia>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true, .replyFlag = (1U << 0) });
	case mtpc_messages_sendInlineBotResult:
		return ReadPeerAllowed<MTPmessages_SendInlineBotResult>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true, .replyFlag = (1U << 0) });
	case mtpc_messages_editMessage:
		return ReadPeerAllowed<MTPmessages_EditMessage>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_getBotCallbackAnswer:
		return ReadPeerAllowed<MTPmessages_GetBotCallbackAnswer>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_saveDraft:
		return ReadPeerAllowed<MTPmessages_SaveDraft>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{
				.flags = true,
				.replyFlag = (1U << 4),
				.replyBeforePeer = true,
			});
	case mtpc_messages_setGameScore:
		return ReadPeerAllowed<MTPmessages_SetGameScore>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_uploadMedia:
		return ReadPeerAllowed<MTPmessages_UploadMedia>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true, .forbiddenFlags = 1U });
	case mtpc_messages_sendScreenshotNotification:
		return ReadPeerAllowed<MTPmessages_SendScreenshotNotification>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .requiredReply = true });
	case mtpc_messages_sendMultiMedia:
		return ReadPeerAllowed<MTPmessages_SendMultiMedia>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true, .replyFlag = (1U << 0) });
	case mtpc_messages_updatePinnedMessage:
		return ReadPeerAllowed<MTPmessages_UpdatePinnedMessage>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_sendVote:
		return ReadPeerAllowed<MTPmessages_SendVote>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_sendScheduledMessages:
		return ReadPeerAllowed<MTPmessages_SendScheduledMessages>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_sendReaction:
		return ReadPeerAllowed<MTPmessages_SendReaction>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_sendBotRequestedPeer:
		return ReadPeerAllowed<MTPmessages_SendBotRequestedPeer>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_sendQuickReplyMessages:
		return ReadPeerAllowed<MTPmessages_SendQuickReplyMessages>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_sendPaidReaction:
		return ReadPeerAllowed<MTPmessages_SendPaidReaction>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_messages_toggleTodoCompleted:
		return ReadPeerAllowed<MTPmessages_ToggleTodoCompleted>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_appendTodoList:
		return ReadPeerAllowed<MTPmessages_AppendTodoList>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_addPollAnswer:
		return ReadPeerAllowed<MTPmessages_AddPollAnswer>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_messages_deletePollAnswer:
		return ReadPeerAllowed<MTPmessages_DeletePollAnswer>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout());
	case mtpc_stories_sendReaction:
		return ReadPeerAllowed<MTPstories_SendReaction>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
	case mtpc_ephemeral_getCallbackAnswer:
		return ReadPeerAllowed<MTPephemeral_GetCallbackAnswer>(
			from,
			end,
			selfId,
			allows,
			PeerRequestLayout{ .flags = true });
