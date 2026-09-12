"""Audit the transport policy when updating Telegram's API schema.

The C++ regression executable tests real TL decoding. These checks additionally
require a policy review when upstream changes the set or shape of API methods.
"""

import hashlib
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Telegram/SourceFiles"
SCHEMA = (SOURCE / "mtproto/scheme/api.tl").read_text(encoding="utf-8")
METHODS = dict(re.findall(
    r"^([\w.]+)#[0-9a-f]+\s*(.*?) = \S+;",
    SCHEMA.split("---functions---", 1)[1],
    re.MULTILINE,
))
SAFE = set(re.findall(
    r"case mtpc_(\w+):",
    (SOURCE / "mtproto/allowlist_safe_requests.inc").read_text(),
))
PEER_RULES = (SOURCE / "mtproto/allowlist_peer_requests.inc").read_text()
PEER_CHECKED = set(re.findall(
    r"case mtpc_(\w+):",
    PEER_RULES,
))


class RequestPolicySchemaTest(unittest.TestCase):
    def test_schema_requires_policy_review(self):
        self.assertEqual(
            hashlib.sha256(SCHEMA.encode()).hexdigest(),
            "7655504c25a5d3a368e7729d3e9e64afffcacbcb54ec85508c60f0472064b617",
            "The Telegram API changed. Review destination checks and the safe "
            "RPC snapshot before updating this reviewed fingerprint.",
        )

    def test_policy_references_existing_rpc_methods(self):
        schema_names = {name.replace(".", "_") for name in METHODS}
        self.assertTrue(SAFE <= schema_names, SAFE - schema_names)
        self.assertTrue(PEER_CHECKED <= schema_names, PEER_CHECKED - schema_names)
        self.assertFalse(SAFE & PEER_CHECKED)

    def test_sends_and_remote_bot_queries_never_bypass_destination_checks(self):
        communications = {
            name.replace(".", "_")
            for name in METHODS
            if name.startswith(("messages.send", "ephemeral.send", "phone.send"))
        } | {
            "messages_forwardMessages", "messages_editMessage",
            "messages_getInlineBotResults", "messages_getBotCallbackAnswer",
            "messages_startBot", "messages_editInlineBotMessage",
            "messages_requestWebView", "messages_requestSimpleWebView",
            "messages_requestAppWebView", "messages_requestMainWebView",
            "messages_getBotApp", "messages_getAttachMenuBot",
            "messages_prolongWebView", "messages_toggleBotInAttachMenu",
            "bots_canSendMessage", "bots_allowSendMessage",
            "ephemeral_getCallbackAnswer", "bots_invokeWebViewCustomMethod",
            "account_updateConnectedBot", "account_updateBusinessGreetingMessage",
            "account_updateBusinessAwayMessage", "phone_requestCall",
            "phone_acceptCall", "phone_joinGroupCall", "stories_sendStory",
            "stories_editStory", "stories_startLive", "contacts_getLocated",
        }
        self.assertFalse(SAFE & communications, SAFE & communications)

    def test_generic_peer_decoder_is_only_used_for_mandatory_input_peers(self):
        for name, params in METHODS.items():
            if name.replace(".", "_") in PEER_CHECKED:
                self.assertIn("peer:InputPeer", params, name)
                self.assertNotIn("peer:flags.", params, name)

    def test_serialized_prefix_layout_matches_schema(self):
        cases = dict(re.findall(
            r"case mtpc_(\w+):(.*?)(?=\n\s*case |\Z)",
            PEER_RULES,
            re.DOTALL,
        ))
        for name, params in METHODS.items():
            rule = cases.get(name.replace(".", "_"))
            if rule is None:
                continue
            self.assertEqual(".flags = true" in rule, params.startswith("flags:#"), name)
            optional_reply = re.search(r"reply_to:flags\.(\d+)\?InputReplyTo", params)
            if optional_reply:
                self.assertIn(f".replyFlag = (1U << {optional_reply[1]})", rule, name)
                self.assertEqual(
                    ".replyBeforePeer = true" in rule,
                    params.index("reply_to:") < params.index("peer:InputPeer"),
                    name,
                )
            self.assertEqual(
                ".requiredReply = true" in rule,
                "reply_to:InputReplyTo" in params,
                name,
            )

    def test_essential_account_startup_and_reading_remain_available(self):
        for name in (
            "auth_sendCode", "auth_signIn", "auth_checkPassword",
            "auth_importAuthorization", "auth_exportAuthorization",
            "auth_bindTempAuthKey", "help_getConfig", "help_getAppConfig",
            "users_getUsers", "updates_getState", "updates_getDifference",
            "messages_getDialogs", "messages_getHistory", "upload_getFile",
        ):
            self.assertIn(name, SAFE)


if __name__ == "__main__":
    unittest.main()
