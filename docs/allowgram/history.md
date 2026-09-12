# History and upstream provenance

[Back to Allowgram](../../README.md)

Allowgram derives from Telegram Desktop upstream commit `272f6f5c2d29d8cdb3aec15907d616b87451a3ca`. The original six Allowgram commits end at `52063613d25b281a9e5f9c360e42b48ef070922e` (7.2.8.3). Their author and committer are the owner, molotovgit. The upstream base and its complete ancestry retain their original contributors and identities.

The original checkout was shallow. Missing ancestry was fetched from the official Telegram Desktop repository into a separate publication checkout; no artificial root or altered upstream history was created. A private backup was retained. The original application/release checkout and privately delivered packages were not rewritten.

`archive/release-7.2.8.3` retains the original commits and full upstream ancestry. The initial public `main` at `c691b2b80cade2c25050116beae3def78226bca1` presents the owner-authored changes as smaller coherent commits. At the end of each original change, Git tree IDs were compared and matched exactly. That publication's application tree is identical to 7.2.8.3 before public documentation additions. This verifies content, including modes, deletions, binary artwork and submodule pins; it does not claim each dependent intermediate split was separately buildable or tested.

Version 7.2.8.4 extends that published history with ordinary forward commits.
Its reviewed application delta is restricted to the allow-list input style and
row layout, native UI regression helpers and packaging revision. Documentation
and fresh screenshots accompany the fix. The archival branch and six original
tree-equivalence boundaries remain unchanged; current application equality to
7.2.8.3 is deliberately replaced by an explicit scoped-delta check.

The new history uses the owner's requested author and committer email, actual new commit times and no forged signatures or assistant coauthor trailers. Repository ownership does not transfer authorship of Telegram or third-party code. Existing license and copyright notices remain intact.

The complete [machine-readable mapping](commit-mapping.json) records all original and replacement commits and tree IDs. The mapping below is for review, not a squashed replacement of the original archive.

### 571b5cb — Add Allowgram messaging allowlist and Windows installer pipeline

Original: [`571b5cb982c6`](https://github.com/molotovgit/allowgram/commit/571b5cb982c68e58d0705b3834345b55f93b2222). Exact tree verified after [`6b23b3c4eabd`](https://github.com/molotovgit/allowgram/commit/6b23b3c4eabded1244854cb29372d1d6cda05b4c).

| Commit | Atomic change |
| --- | --- |
| [`233df18c1cea`](https://github.com/molotovgit/allowgram/commit/233df18c1ceacd87752845b3e8b0c76fae2b37fd) | Maintain a manual Allowgram Windows build workflow |
| [`9bfcf1fc1747`](https://github.com/molotovgit/allowgram/commit/9bfcf1fc174709ae106dedb733eb295fbd28cfa2) | Exclude local Allowgram credentials and build files |
| [`3e5467cc89ea`](https://github.com/molotovgit/allowgram/commit/3e5467cc89ea9912cc72ed4632a5db4c5dad4ca8) | Document Allowgram behavior and verified boundaries |
| [`1e7923af97ab`](https://github.com/molotovgit/allowgram/commit/1e7923af97abbc91a324de8224bcba05411cd9df) | Register Allowgram implementation sources in CMake |
| [`694d20eb5652`](https://github.com/molotovgit/allowgram/commit/694d20eb56528583fddcb4bcc7e5b294e6c1a61f) | Document the Allowgram artwork source |
| [`2397edb724a5`](https://github.com/molotovgit/allowgram/commit/2397edb724a51319c95765f2886e12a6a32eae99) | Add Allowgram icons and reproducible artwork |
| [`1043bdec4cfa`](https://github.com/molotovgit/allowgram/commit/1043bdec4cfa6ce6fdb140213be88efe4adcfd61) | Define Allowgram setup labels and validation messages |
| [`4b67d1b8c149`](https://github.com/molotovgit/allowgram/commit/4b67d1b8c149d611b2d881f094f1880b18a1921e) | Add Allowgram telegram |
| [`c7288f050406`](https://github.com/molotovgit/allowgram/commit/c7288f050406a0e0c5aa2bd16ed424920920e633) | Add Allowgram Telegram |
| [`515def7de917`](https://github.com/molotovgit/allowgram/commit/515def7de9173e471a5bbcb22b68bca7ac810b19) | Add Allowgram config |
| [`b07e469d942d`](https://github.com/molotovgit/allowgram/commit/b07e469d942d3e0567eb4ce10dd43833900450b8) | Add Allowgram application |
| [`8f91768d3038`](https://github.com/molotovgit/allowgram/commit/8f91768d3038f90c6965daea12b4d6fcf8d07502) | Add Allowgram launcher |
| [`57d57139d8eb`](https://github.com/molotovgit/allowgram/commit/57d57139d8eb7279413bd8f13da9d508aff3c5b6) | Add Allowgram version |
| [`929300b9d941`](https://github.com/molotovgit/allowgram/commit/929300b9d941eff6da5b221ce04fa37c91f49d26) | Add Allowgram data chat participant status |
| [`98f9f1ab789f`](https://github.com/molotovgit/allowgram/commit/98f9f1ab789f17e660ccc1ba9592cb42e53cfd07) | Add Allowgram data peer values |
| [`9a556a9cc77b`](https://github.com/molotovgit/allowgram/commit/9a556a9cc77b4e121b8b3c309b7753c56a18c2d5) | Add Allowgram intro |
| [`6a177b62716c`](https://github.com/molotovgit/allowgram/commit/6a177b62716ce86e38b5214c3b88132679f91642) | Add Allowgram intro start |
| [`1d4cf600afff`](https://github.com/molotovgit/allowgram/commit/1d4cf600afff24e716471c0397529560f25a0c29) | Add Allowgram intro step |
| [`a4ce9d5a304c`](https://github.com/molotovgit/allowgram/commit/a4ce9d5a304ca039b3e13d47f2284dc28f457cd7) | Add Allowgram allowlist policy |
| [`a044d0c52b65`](https://github.com/molotovgit/allowgram/commit/a044d0c52b659ee63c8470d24801b211d6b852dc) | Add Allowgram main account |
| [`7d496158e312`](https://github.com/molotovgit/allowgram/commit/7d496158e31218420caca29034d63875a0640076) | Add Allowgram main session |
| [`5a9933812d74`](https://github.com/molotovgit/allowgram/commit/5a9933812d74d1dd64fcdfb6409a29caca5bfb75) | Add Allowgram main session settings |
| [`dec6e9229d9a`](https://github.com/molotovgit/allowgram/commit/dec6e9229d9ae451eb5d8a366935c33f46cf54be) | Add Allowgram mainwindow |
| [`38577eb51d45`](https://github.com/molotovgit/allowgram/commit/38577eb51d45ee57a2d80e650e24b5258765f844) | Add Allowgram allowlist peer requests |
| [`6e59f63c70ec`](https://github.com/molotovgit/allowgram/commit/6e59f63c70ec3a767c9c4ea0b9a3d025d52b3328) | Add Allowgram allowlist request guard |
| [`de5f012ec470`](https://github.com/molotovgit/allowgram/commit/de5f012ec470578aa0563047ed3e94327d9a1d96) | Add Allowgram allowlist safe requests |
| [`5c01fe88d622`](https://github.com/molotovgit/allowgram/commit/5c01fe88d6220990e4f9c5530eda5c043b2d37bc) | Add Allowgram mtp instance |
| [`3ff7840857cb`](https://github.com/molotovgit/allowgram/commit/3ff7840857cbae30f8d36f891f5fd78d40ca2c63) | Add Allowgram specific win |
| [`f7e5292de52c`](https://github.com/molotovgit/allowgram/commit/f7e5292de52c4a74d45b0510c8b112253e9337d0) | Add Allowgram windows app user model id |
| [`ce8640c6baa2`](https://github.com/molotovgit/allowgram/commit/ce8640c6baa27724f7b2cd9513a3d83a2eac47b7) | Add Allowgram windows toast activator |
| [`f82c9dc11db5`](https://github.com/molotovgit/allowgram/commit/f82c9dc11db5d7c9ccd78d9dd8f1b97f1103a72b) | Add Allowgram storage account |
| [`ada176167720`](https://github.com/molotovgit/allowgram/commit/ada17616772019f2ef411096bb65cc1c06469ed3) | Cover allowlist policy regressions |
| [`dde6b6e14a0d`](https://github.com/molotovgit/allowgram/commit/dde6b6e14a0dfc05f0d9329c73bf83f345c06517) | Cover allowlist request guard regressions |
| [`eeac9cf69ad1`](https://github.com/molotovgit/allowgram/commit/eeac9cf69ad13a69eac64b8c210d966726c7357f) | Cover allowlist request schema regressions |
| [`086e6d2d7f0f`](https://github.com/molotovgit/allowgram/commit/086e6d2d7f0f29a7dd75ddbd2ebcd9c17d80df60) | Add Allowgram window |
| [`47934c9fbd28`](https://github.com/molotovgit/allowgram/commit/47934c9fbd28b3494b16170b2bb534275c66aec4) | Add Allowgram window allowlist |
| [`ab508c27afd2`](https://github.com/molotovgit/allowgram/commit/ab508c27afd28f38ec0b4f1d40518a5dce57cc9c) | Add Allowgram window controller |
| [`38241fef1e1c`](https://github.com/molotovgit/allowgram/commit/38241fef1e1ce06e751b241b6b4e2c5723eda25e) | Maintain Allowgram archive source |
| [`5b1360f63233`](https://github.com/molotovgit/allowgram/commit/5b1360f63233e96d5d034c542e9931d313495421) | Maintain Allowgram package |
| [`ed518fa3e87e`](https://github.com/molotovgit/allowgram/commit/ed518fa3e87ed6038fbd2e2ff30d3d011a56f817) | Maintain Allowgram prepare release |
| [`daa9d69ea245`](https://github.com/molotovgit/allowgram/commit/daa9d69ea245ca9ad9ce64a5cd4b65e6304b96fb) | Maintain Allowgram setup |
| [`ba0a2c474782`](https://github.com/molotovgit/allowgram/commit/ba0a2c474782893e9178b6e4886cb1077ba6e4c9) | Maintain Allowgram test request guard |
| [`85d0c25aa8c4`](https://github.com/molotovgit/allowgram/commit/85d0c25aa8c4fdb12803165f91dbe3ec7717a012) | Document building allowgram win |
| [`2f0891635294`](https://github.com/molotovgit/allowgram/commit/2f0891635294b0c770081392a2313c161373c361) | Document telegram api credentials.example |
| [`0d592fc759c4`](https://github.com/molotovgit/allowgram/commit/0d592fc759c4b3167f19704f0d74ffd36fa2196b) | Archive upstream canary-bot-api automation |
| [`f24d4e2139b1`](https://github.com/molotovgit/allowgram/commit/f24d4e2139b1c1fecf7263fc43cf09256d113422) | Archive upstream canary automation |
| [`534b89cfbd22`](https://github.com/molotovgit/allowgram/commit/534b89cfbd221e404450e74c27a0db81ddd49f2b) | Archive upstream cant-reproduce automation |
| [`7dfa81d251c3`](https://github.com/molotovgit/allowgram/commit/7dfa81d251c37250e4233c69f9caef6b204979cf) | Archive upstream changelog automation |
| [`9fdfdb2d2df1`](https://github.com/molotovgit/allowgram/commit/9fdfdb2d2df172c131825c9b6c89dbcb3e9d1017) | Archive upstream copyright_year_updater automation |
| [`a2303ba59903`](https://github.com/molotovgit/allowgram/commit/a2303ba59903fb7d7f04878b2137f047ecec5de9) | Archive upstream docker automation |
| [`db3f75676a6e`](https://github.com/molotovgit/allowgram/commit/db3f75676a6ead1dea1eabb3dc53c63b0582df32) | Archive upstream issue_closer automation |
| [`b34308ecae03`](https://github.com/molotovgit/allowgram/commit/b34308ecae03f93d96879c4192da4fcc82eaf794) | Archive upstream linux automation |
| [`bac1d16ee1bf`](https://github.com/molotovgit/allowgram/commit/bac1d16ee1bf314e0043387774873799683155ad) | Archive upstream lock automation |
| [`9c0ba7ff7b27`](https://github.com/molotovgit/allowgram/commit/9c0ba7ff7b270507bfbd671d27a86d60c8fca417) | Archive upstream mac automation |
| [`8092455c220d`](https://github.com/molotovgit/allowgram/commit/8092455c220d3238c2c3364fb8d1e86b97be03dd) | Archive upstream mac_packaged automation |
| [`c0be2b3a84ea`](https://github.com/molotovgit/allowgram/commit/c0be2b3a84ea3eaffb53ae5d413dd54c416170e6) | Archive upstream master_updater automation |
| [`0edf41aff921`](https://github.com/molotovgit/allowgram/commit/0edf41aff921849be57c02799aa8bfe8ecd73b28) | Archive upstream needs-user-action automation |
| [`1da894e1ec67`](https://github.com/molotovgit/allowgram/commit/1da894e1ec673030e14bc136ce72a896eebe9ae7) | Archive upstream snap automation |
| [`dab8cd4474de`](https://github.com/molotovgit/allowgram/commit/dab8cd4474ded806aad685fc46796fd7671e7efb) | Archive upstream stale automation |
| [`5826d2bbd405`](https://github.com/molotovgit/allowgram/commit/5826d2bbd405c4fe2d912d7161820db5271126c1) | Archive upstream unused_styles_updater automation |
| [`1f147dc98874`](https://github.com/molotovgit/allowgram/commit/1f147dc98874f3dccc3b7a14dd0e91daa5847c31) | Archive upstream user_agent_updater automation |
| [`2bc9f748162e`](https://github.com/molotovgit/allowgram/commit/2bc9f748162e2ac11a2f3bf5af9f832d700fde73) | Archive upstream waiting-for-answer automation |
| [`d9f6c641ca7c`](https://github.com/molotovgit/allowgram/commit/d9f6c641ca7ca0eb4766c7270da24b962f5b7141) | Archive upstream win automation |
| [`6b23b3c4eabd`](https://github.com/molotovgit/allowgram/commit/6b23b3c4eabded1244854cb29372d1d6cda05b4c) | Archive upstream winget automation |

### 508dcec — Finish Windows build and isolate Allowgram account data

Original: [`508dcece8b1f`](https://github.com/molotovgit/allowgram/commit/508dcece8b1f4f68be5d111af9d528f35bfbaaa9). Exact tree verified after [`c41da5d9b92f`](https://github.com/molotovgit/allowgram/commit/c41da5d9b92f773eb105fc6677fea6f70ef76ef0).

| Commit | Atomic change |
| --- | --- |
| [`ffa42f2bb355`](https://github.com/molotovgit/allowgram/commit/ffa42f2bb3551340f39349e9cdb34ed9e15cd6e1) | Maintain a manual Allowgram Windows build workflow |
| [`939173fb5101`](https://github.com/molotovgit/allowgram/commit/939173fb5101d86ad789405427a67953d88d04e8) | Document Allowgram behavior and verified boundaries |
| [`de6b4ba5a50f`](https://github.com/molotovgit/allowgram/commit/de6b4ba5a50f3425df9a0cc1fabf47320ce2c917) | Refine Allowgram launcher |
| [`b24aa0adc8a9`](https://github.com/molotovgit/allowgram/commit/b24aa0adc8a96137cb1672285c92dec4a1219f36) | Refine Allowgram logs |
| [`74959241e1c3`](https://github.com/molotovgit/allowgram/commit/74959241e1c355e569d1b2c92c33bc820450f2a1) | Maintain Allowgram package |
| [`bb86feb00e9f`](https://github.com/molotovgit/allowgram/commit/bb86feb00e9f044b546d6a13b071aa77ab87b7d5) | Maintain Allowgram prepare release |
| [`ee86e3f1e063`](https://github.com/molotovgit/allowgram/commit/ee86e3f1e063f608688246734bd9d725c1af4d4a) | Maintain Allowgram setup |
| [`c41da5d9b92f`](https://github.com/molotovgit/allowgram/commit/c41da5d9b92f773eb105fc6677fea6f70ef76ef0) | Document building allowgram win |

### 5eee4c0 — Fix Windows PowerShell packaging script initialization

Original: [`5eee4c03b034`](https://github.com/molotovgit/allowgram/commit/5eee4c03b034c72bf78366ebffa92a78d85169ef). Exact tree verified after [`75d20413b22d`](https://github.com/molotovgit/allowgram/commit/75d20413b22d4d8c5713b8672779744e9827413d).

| Commit | Atomic change |
| --- | --- |
| [`75d20413b22d`](https://github.com/molotovgit/allowgram/commit/75d20413b22d4d8c5713b8672779744e9827413d) | Fix Windows PowerShell packaging script initialization |

### 3f162aa — Preserve portable account directory in Windows ZIP

Original: [`3f162aa1b89a`](https://github.com/molotovgit/allowgram/commit/3f162aa1b89add34d661bb7df0acca068101cf0d). Exact tree verified after [`372690ffd721`](https://github.com/molotovgit/allowgram/commit/372690ffd7216d37d34305e7e703c56e5dd7267a).

| Commit | Atomic change |
| --- | --- |
| [`372690ffd721`](https://github.com/molotovgit/allowgram/commit/372690ffd7216d37d34305e7e703c56e5dd7267a) | Preserve portable account directory in Windows ZIP |

### 4a9ddfa — Hide excluded chats and add repeatable allow-list rows

Original: [`4a9ddfa5182d`](https://github.com/molotovgit/allowgram/commit/4a9ddfa5182d02eecfeaeaa9704becbcfd3071ba). Exact tree verified after [`eab43887bd13`](https://github.com/molotovgit/allowgram/commit/eab43887bd1332cbb4503342d850d29db5d6f658).

| Commit | Atomic change |
| --- | --- |
| [`0ea41f1b92b5`](https://github.com/molotovgit/allowgram/commit/0ea41f1b92b50100d38cf7791d2f1e590746b7f3) | Document Allowgram behavior and verified boundaries |
| [`065b7a9153c3`](https://github.com/molotovgit/allowgram/commit/065b7a9153c340550efba37b4105d1ec2945beb3) | Register Allowgram implementation sources in CMake |
| [`81250a35f227`](https://github.com/molotovgit/allowgram/commit/81250a35f227c2a3a11f07c2df969449e0e5f9e7) | Define Allowgram setup labels and validation messages |
| [`ca294c82decc`](https://github.com/molotovgit/allowgram/commit/ca294c82decc21952206ded706207a8654cfbab2) | Filter Allowgram api messages search |
| [`be051b5625fd`](https://github.com/molotovgit/allowgram/commit/be051b5625fd8047b1f559d8d419f5f29304ffb5) | Filter Allowgram api updates |
| [`fb04a2993426`](https://github.com/molotovgit/allowgram/commit/fb04a29934268584c287c06f2cb8d4d72907f8b6) | Filter Allowgram edit filter box |
| [`7104abebb610`](https://github.com/molotovgit/allowgram/commit/7104abebb610ffcfe95e4281bb40cbf8e205eddc) | Filter Allowgram edit filter chats list |
| [`8f613635d5a8`](https://github.com/molotovgit/allowgram/commit/8f613635d5a8cd68250961897600c3ad2febcc3b) | Filter Allowgram edit filter chats preview |
| [`c5c26f4623ef`](https://github.com/molotovgit/allowgram/commit/c5c26f4623ef14b5fd5d30201f4f8deb7d5dfa73) | Filter Allowgram edit filter links |
| [`99c8e15c72ce`](https://github.com/molotovgit/allowgram/commit/99c8e15c72ce163533becfd8668622e9b2dfd673) | Filter Allowgram peer list box |
| [`79de68b5f4ca`](https://github.com/molotovgit/allowgram/commit/79de68b5f4cade89c86de6e18daff969709a36c3) | Filter Allowgram peer list controllers |
| [`3fb3b572c41f`](https://github.com/molotovgit/allowgram/commit/3fb3b572c41f19d18ef2ddc65f681e27cf0be6e0) | Filter Allowgram calls box controller |
| [`079c11b9ba90`](https://github.com/molotovgit/allowgram/commit/079c11b9ba90004ee3be782d96ea0fbaa4aca4d9) | Filter Allowgram calls instance |
| [`1245c972bc09`](https://github.com/molotovgit/allowgram/commit/1245c972bc0963c80755a93415d39e9aaf2e84b6) | Filter Allowgram application |
| [`017f5b99dd1c`](https://github.com/molotovgit/allowgram/commit/017f5b99dd1cbde36bb06184cf0db97e9b82609d) | Filter Allowgram scheduled messages |
| [`f288831489da`](https://github.com/molotovgit/allowgram/commit/f288831489da3bef86c6d7d7324c060696f6ea4e) | Filter Allowgram data chat filters |
| [`bb9da0d09494`](https://github.com/molotovgit/allowgram/commit/bb9da0d0949437719285c69f7bcefc5698b55d88) | Filter Allowgram data folder |
| [`29ba2b30ca5b`](https://github.com/molotovgit/allowgram/commit/29ba2b30ca5b39d7e0d6071e9a15aa10ad8d547f) | Filter Allowgram data saved sublist |
| [`e5d6c4f8b7eb`](https://github.com/molotovgit/allowgram/commit/e5d6c4f8b7ebbba6dab9fb5c957f4da6c6896701) | Filter Allowgram data search controller |
| [`bd2123c6fe61`](https://github.com/molotovgit/allowgram/commit/bd2123c6fe61e28a6f766d87dfe7b6b202628cf1) | Filter Allowgram data session |
| [`a9657963da6c`](https://github.com/molotovgit/allowgram/commit/a9657963da6c08a8d56164dd7b21821fbff14b82) | Filter Allowgram data stories |
| [`ed432c61375f`](https://github.com/molotovgit/allowgram/commit/ed432c61375f7b5fb0961a986d2ed35427524336) | Filter Allowgram dialogs inner widget |
| [`4e3a3c30a7a4`](https://github.com/molotovgit/allowgram/commit/4e3a3c30a7a4d6ed4be25a919d9b1dfd883aa636) | Filter Allowgram dialogs search posts |
| [`fc77661b04a0`](https://github.com/molotovgit/allowgram/commit/fc77661b04a08636b4ad9d85a17f36d0fd69a579) | Filter Allowgram dialogs widget |
| [`a56e0e8f00ad`](https://github.com/molotovgit/allowgram/commit/a56e0e8f00ad2d8f72b213b7945e3c806be74396) | Filter Allowgram suggestion birthday contacts |
| [`9a2262ac9091`](https://github.com/molotovgit/allowgram/commit/9a2262ac90911b1c5bd37ebc58a9c3318cb4fd1f) | Filter Allowgram dialogs suggestions |
| [`c177033a3eac`](https://github.com/molotovgit/allowgram/commit/c177033a3eac67789fe42076ab869762a329662f) | Filter Allowgram export manager |
| [`9a6a958acb9a`](https://github.com/molotovgit/allowgram/commit/9a6a958acb9adf1dafb220b0404ca63b0192b4d9) | Filter Allowgram export view panel controller |
| [`f0a58e1e2aea`](https://github.com/molotovgit/allowgram/commit/f0a58e1e2aea04d300e3dfdf3a593345425f4f1f) | Filter Allowgram history |
| [`145ca8816542`](https://github.com/molotovgit/allowgram/commit/145ca8816542403e12ee3c8466b23195117be0c5) | Filter Allowgram history unread things |
| [`70aa1174391f`](https://github.com/molotovgit/allowgram/commit/70aa1174391f38db7077fa0b5835e0c1e28c3e98) | Filter Allowgram history widget |
| [`c20657e2eb46`](https://github.com/molotovgit/allowgram/commit/c20657e2eb4638beabfa70c994d7af39ebb8c971) | Filter Allowgram info common groups inner widget |
| [`6915d5037d6e`](https://github.com/molotovgit/allowgram/commit/6915d5037d6e011b64afea2125bd358d5648a7dc) | Filter Allowgram info content widget |
| [`1456dfa970ed`](https://github.com/molotovgit/allowgram/commit/1456dfa970edd5b39ace325ab8829d8d85b3c7e4) | Filter Allowgram info controller |
| [`c0cd2097f643`](https://github.com/molotovgit/allowgram/commit/c0cd2097f6431beb6319a85435161d38a6a9d827) | Filter Allowgram info memento |
| [`fe981367102e`](https://github.com/molotovgit/allowgram/commit/fe981367102e9ce87423e1c76d745e154fd18434) | Filter Allowgram info wrap widget |
| [`03658dd5d1fb`](https://github.com/molotovgit/allowgram/commit/03658dd5d1fbb710f637ac0f622ce9602c50cce3) | Filter Allowgram info profile values |
| [`c97e06c10d80`](https://github.com/molotovgit/allowgram/commit/c97e06c10d80315331b60d6182546cc8e4ae0a2c) | Filter Allowgram info similar peers widget |
| [`24c12520a4bc`](https://github.com/molotovgit/allowgram/commit/24c12520a4bcfac0298b3a862a8925ad4822f83e) | Filter Allowgram iv instance |
| [`df3763664b9c`](https://github.com/molotovgit/allowgram/commit/df3763664b9c225d1c6327802cfe2b4f1a0645f6) | Filter Allowgram iv rich message html export |
| [`169b0f2bd5bd`](https://github.com/molotovgit/allowgram/commit/169b0f2bd5bdbd2a72f0cb17d5c276d9d03d5c8d) | Filter Allowgram mainwidget |
| [`d3321c7efb3f`](https://github.com/molotovgit/allowgram/commit/d3321c7efb3f60ba971b7d07311b3f38f208e412) | Filter Allowgram media player instance |
| [`d0b22303a0b3`](https://github.com/molotovgit/allowgram/commit/d0b22303a0b372714a7a7dabc13fd2fd0f8f8823) | Filter Allowgram system media controls manager |
| [`7488201ae212`](https://github.com/molotovgit/allowgram/commit/7488201ae212022917aea2a461eba349d5b21785) | Filter Allowgram allowlist message guard |
| [`3b831263e7dd`](https://github.com/molotovgit/allowgram/commit/3b831263e7dd12444a40dcb46fb45bb95e28998b) | Filter Allowgram allowlist peer requests |
| [`20e9118eb6c7`](https://github.com/molotovgit/allowgram/commit/20e9118eb6c7a4d7ec79e3214059e33e9686a784) | Filter Allowgram allowlist request guard |
| [`b4689e875bcc`](https://github.com/molotovgit/allowgram/commit/b4689e875bcc22591e12ff582f6f450ec958000d) | Filter Allowgram allowlist safe requests |
| [`52cd055f683d`](https://github.com/molotovgit/allowgram/commit/52cd055f683dfbeade515fd1bd20aeb0be627238) | Filter Allowgram notifications manager win |
| [`66ab35720fac`](https://github.com/molotovgit/allowgram/commit/66ab35720facac56bd4a0aa84dc9e36146cd9b1e) | Filter Allowgram settings recipients helper |
| [`abd9ef34db07`](https://github.com/molotovgit/allowgram/commit/abd9ef34db07936d1f00e5ae779c4f6be30e0fca) | Filter Allowgram settings advanced |
| [`adc854a80064`](https://github.com/molotovgit/allowgram/commit/adc854a80064c30b6c24d86866aa1bb4742296c4) | Filter Allowgram settings chat |
| [`2dfc32099977`](https://github.com/molotovgit/allowgram/commit/2dfc320999773588ced604b0c863d42753729dbf) | Cover allowlist policy regressions |
| [`4b9f8fb15c3c`](https://github.com/molotovgit/allowgram/commit/4b9f8fb15c3c3c5d82d707e826011ee009700bc7) | Cover allowlist request guard regressions |
| [`7ebda0b8c98a`](https://github.com/molotovgit/allowgram/commit/7ebda0b8c98af524d5162545323ad33d61eea6cc) | Filter Allowgram notifications manager |
| [`5dcd30f12aec`](https://github.com/molotovgit/allowgram/commit/5dcd30f12aec27efaa43de71d4d40bd324d45a52) | Filter Allowgram notifications manager default |
| [`e9e3c4f561b0`](https://github.com/molotovgit/allowgram/commit/e9e3c4f561b09287db66b2e4f72f252c85b62b48) | Filter Allowgram window |
| [`3ada27707b4d`](https://github.com/molotovgit/allowgram/commit/3ada27707b4d3f2d048f71c9e3ddceb7316f4bcd) | Filter Allowgram window allowlist |
| [`954274d385b9`](https://github.com/molotovgit/allowgram/commit/954274d385b97d5944ff356162031bf38526d37b) | Filter Allowgram window peer menu |
| [`d09ceef1ad88`](https://github.com/molotovgit/allowgram/commit/d09ceef1ad88cd1baca2ac83290863db051b3b1a) | Filter Allowgram window separate id |
| [`31e28f7a7d01`](https://github.com/molotovgit/allowgram/commit/31e28f7a7d01f52be7b9b650cfd33c4885f1c746) | Filter Allowgram window session controller |
| [`8b9dc6a19839`](https://github.com/molotovgit/allowgram/commit/8b9dc6a19839513d452994a1f0d699ce4d69cc64) | Maintain Allowgram package |
| [`247f2e3f0608`](https://github.com/molotovgit/allowgram/commit/247f2e3f0608d93ab7d8e3602512ba6254f673a4) | Set the Allowgram installer packaging revision |
| [`eab43887bd13`](https://github.com/molotovgit/allowgram/commit/eab43887bd1332cbb4503342d850d29db5d6f658) | Maintain Allowgram test request guard |

### 5206361 — Allow Mini Apps for explicitly allowed bots

Original: [`52063613d25b`](https://github.com/molotovgit/allowgram/commit/52063613d25b281a9e5f9c360e42b48ef070922e). Exact tree verified after [`a15ecc3d7612`](https://github.com/molotovgit/allowgram/commit/a15ecc3d76122674578e999fe2337a43513f4093).

| Commit | Atomic change |
| --- | --- |
| [`de4f7ce76bd1`](https://github.com/molotovgit/allowgram/commit/de4f7ce76bd125cabc0492612a06fe3401a4753c) | Document Allowgram behavior and verified boundaries |
| [`37321b6c3661`](https://github.com/molotovgit/allowgram/commit/37321b6c366102b65fd034bb56576c192110155d) | Register Allowgram implementation sources in CMake |
| [`4b774dd30e78`](https://github.com/molotovgit/allowgram/commit/4b774dd30e78e80611a9aed8eae2cb68f6baef7c) | Authorize Allowgram api bot |
| [`be0f47ab6af8`](https://github.com/molotovgit/allowgram/commit/be0f47ab6af8b3888909ba8f0a9430a0763864db) | Declare bot app lifetime and authorization checks |
| [`fb3f6b4e9d23`](https://github.com/molotovgit/allowgram/commit/fb3f6b4e9d239dfa383bec4e1f2227b69f4c160b) | Bind bot app contexts and preserve explicit actions |
| [`647e9054f8c7`](https://github.com/molotovgit/allowgram/commit/647e9054f8c7b104c6f43c45e08dd2236a3c029c) | Revalidate bot app lifetime and resolved ownership |
| [`2623b0cec36a`](https://github.com/molotovgit/allowgram/commit/2623b0cec36a0bd0e0b36c6116915df7c60247a2) | Keep bot app confirmation and write consent explicit |
| [`b52ff27dfc28`](https://github.com/molotovgit/allowgram/commit/b52ff27dfc2802044c3ac41de1fdec230fab5e2b) | Authorize each bot app request before launch |
| [`aa8f77ee6669`](https://github.com/molotovgit/allowgram/commit/aa8f77ee6669e35a2ca5d186748f2cc2703ad5c1) | Close bot apps when query prolongation is denied |
| [`50cd826bab0a`](https://github.com/molotovgit/allowgram/commit/50cd826bab0a6cfd8be30813486edf24d6f96c6b) | Resolve in-app Telegram links under the allow-list |
| [`68b565c96674`](https://github.com/molotovgit/allowgram/commit/68b565c966746d6c8016c699c241f36091ac2041) | Recheck recipients and permissions in app callbacks |
| [`1cd0daac7139`](https://github.com/molotovgit/allowgram/commit/1cd0daac713976ee351ec0c7830e56a7dc6984a7) | Disable unsupported Mini App mutation bridges |
| [`dc0efe9e036c`](https://github.com/molotovgit/allowgram/commit/dc0efe9e036c01673f10ae1c5707188bce954a2a) | Guard late downloads and bot privacy navigation |
| [`b5170752934d`](https://github.com/molotovgit/allowgram/commit/b5170752934db596378fa10078fe396e0ee432a8) | Bind app menu consent to the active account |
| [`0e279d85be1d`](https://github.com/molotovgit/allowgram/commit/0e279d85be1d4f6d6f40a005c31bf52c925430d9) | Authorize Allowgram main account |
| [`5926d3c57a8e`](https://github.com/molotovgit/allowgram/commit/5926d3c57a8edce66f3b1d427f443ae7612fd6b8) | Authorize Allowgram allowlist request guard |
| [`c2160682fdc4`](https://github.com/molotovgit/allowgram/commit/c2160682fdc4bbfa1d9cf8015e7c4f1008901a80) | Authorize Allowgram allowlist safe requests |
| [`c24389337595`](https://github.com/molotovgit/allowgram/commit/c24389337595153cdb4d0c7496e6cfdf290bda1e) | Authorize Allowgram allowlist webview guard |
| [`6ab7cedd35dc`](https://github.com/molotovgit/allowgram/commit/6ab7cedd35dcfec619c526f416615dd19e0d3b51) | Cover allowlist request guard regressions |
| [`7ece5b44c4b8`](https://github.com/molotovgit/allowgram/commit/7ece5b44c4b8bd605f420c0d5a658241b0b613ee) | Cover allowlist request schema regressions |
| [`d2dbe02a3421`](https://github.com/molotovgit/allowgram/commit/d2dbe02a3421da93b026e38b8e1380fc64a83178) | Cover allowlist webview regressions |
| [`5c69fffc37c4`](https://github.com/molotovgit/allowgram/commit/5c69fffc37c44b0053987dba2b2fc09d56b49054) | Authorize Allowgram attach bot webview |
| [`d69b0e0afab7`](https://github.com/molotovgit/allowgram/commit/d69b0e0afab77de904f93e90eb1de6a7e0111ef7) | Authorize Allowgram window session controller |
| [`964ea62a0265`](https://github.com/molotovgit/allowgram/commit/964ea62a02652bd0552082a8e7b65478df498809) | Maintain Allowgram package |
| [`c4633be16c62`](https://github.com/molotovgit/allowgram/commit/c4633be16c62151565448cc93d47115b3ef90f6a) | Set the Allowgram installer packaging revision |
| [`e84ca2fa956d`](https://github.com/molotovgit/allowgram/commit/e84ca2fa956d4cd99181ef75e43560a98d12bd38) | Maintain Allowgram test request guard |
| [`a15ecc3d7612`](https://github.com/molotovgit/allowgram/commit/a15ecc3d76122674578e999fe2337a43513f4093) | Document allowgram 7.2.8.3 |
