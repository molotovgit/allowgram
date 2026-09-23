SheetWidgetTest::Check(session->allowlistConfigured(), "real configureAllowlist completed verified save");
SheetWidgetTest::Check(session->allowlistPeers().size() == 5,
	"only matched row or manually entered typed peers persisted");
SheetWidgetTest::Check(session->allowlistPeers().contains(peerFromChannel(ChannelId(101)))
	&& session->allowlistPeers().contains(peerFromChannel(ChannelId(102))),
	"Bot API group IDs converted to native channel IDs");
SheetWidgetTest::Check(_manual == (qEnvironmentVariable("ALLOWGRAM_SHEET_SCENE") == u"absent"_q),
	"matched account never constructed manual input fields");
QCoreApplication::instance()->setProperty("sheetSaved", true);
