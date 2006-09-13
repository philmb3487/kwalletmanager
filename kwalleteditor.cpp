/*
   Copyright (C) 2003-2005 George Staikos <staikos@kde.org>
   Copyright (C) 2005 Isaac Clerencia <isaac@warp.es>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/


#include "kbetterthankdialogbase.h"
#include "kwalleteditor.h"
#include "kwmapeditor.h"
#include "allyourbase.h"

#include <stdlib.h>
#include <QDomElement>
#include <QDomNode>
#include <QDomDocument>
#include <kaction.h>
#include <kdebug.h>
#include <kdialog.h>
#include <kfiledialog.h>
#include <k3iconview.h>
#include <kinputdialog.h>
#include <kio/netaccess.h>
#include <k3listviewsearchline.h>
#include <klocale.h>
#include <kcodecs.h>
#include <kmessagebox.h>
#include <kmenu.h>
#include <ksqueezedtextlabel.h>
#include <kstandarddirs.h>
#include <kstdaction.h>
#include <kstringhandler.h>
#include <ktempfile.h>
#include <kxmlguifactory.h>
#include <QCheckBox>
#include <QComboBox>
#include <qclipboard.h>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <q3listview.h>
#include <q3ptrstack.h>
#include <QPushButton>
#include <QTextDocument>
#include <q3textedit.h>
#include <QTimer>
#include <q3widgetstack.h>
//Added by qt3to4:
#include <QTextStream>
#include <QList>
#include <QVBoxLayout>

#include <assert.h>
#include <ktoolbar.h>
#include <kicon.h>

KWalletEditor::KWalletEditor(const QString& wallet, bool isPath, QWidget *parent, const char *name)
: KMainWindow(parent), _walletName(wallet), _nonLocal(isPath) {
	setObjectName(name);
	_newWallet = false;
	_ww = new WalletWidget(this, "Wallet Widget");
	_copyPassAction = KStdAction::copy(this, SLOT(copyPassword()), actionCollection());

	QVBoxLayout *box = new QVBoxLayout(_ww->_entryListFrame);
	box->setSpacing( KDialog::spacingHint() );
	box->setMargin( KDialog::marginHint() );
	_entryList = new KWalletEntryList(_ww->_entryListFrame, "Wallet Entry List");
	box->addWidget(new K3ListViewSearchLineWidget(_entryList, _ww->_entryListFrame));
	box->addWidget(_entryList);

	_ww->_entryStack->setEnabled(true);

	box = new QVBoxLayout(_ww->_entryStack->widget(2));
	_mapEditorShowHide = new QCheckBox(i18n("&Show values"), _ww->_entryStack->widget(2));
	connect(_mapEditorShowHide, SIGNAL(toggled(bool)), this, SLOT(showHideMapEditorValue(bool)));
	_mapEditor = new KWMapEditor(_currentMap, _ww->_entryStack->widget(2));
	box->addWidget(_mapEditorShowHide);
	box->addWidget(_mapEditor);

	setCentralWidget(_ww);

	resize(600, 400);

	connect(_entryList, SIGNAL(selectionChanged(Q3ListViewItem*)),
		this, SLOT(entrySelectionChanged(Q3ListViewItem*)));
	connect(_entryList,
		SIGNAL(contextMenuRequested(Q3ListViewItem*,const QPoint&,int)),
		this,
		SLOT(listContextMenuRequested(Q3ListViewItem*,const QPoint&,int)));
	connect(_entryList,
		SIGNAL(itemRenamed(Q3ListViewItem*, int, const QString&)),
		this,
		SLOT(listItemRenamed(Q3ListViewItem*, int, const QString&)));

	connect(_ww->_passwordValue, SIGNAL(textChanged()),
		this, SLOT(entryEditted()));
	connect(_mapEditor, SIGNAL(dirty()),
		this, SLOT(entryEditted()));

	connect(_ww->_undoChanges, SIGNAL(clicked()),
		this, SLOT(restoreEntry()));
	connect(_ww->_saveChanges, SIGNAL(clicked()),
		this, SLOT(saveEntry()));

	connect(_ww->_showContents, SIGNAL(clicked()),
		this, SLOT(showPasswordContents()));
	connect(_ww->_hideContents, SIGNAL(clicked()),
		this, SLOT(hidePasswordContents()));

	_walletIsOpen = false;

	_w = KWallet::Wallet::openWallet(wallet, winId(), isPath ? KWallet::Wallet::Path : KWallet::Wallet::Asynchronous);
	if (_w) {
		connect(_w, SIGNAL(walletOpened(bool)), this, SLOT(walletOpened(bool)));
		connect(_w, SIGNAL(walletClosed()), this, SLOT(walletClosed()));
		connect(_w, SIGNAL(folderUpdated(const QString&)), this, SLOT(updateEntries(const QString&)));
		connect(_w, SIGNAL(folderListUpdated()), this, SLOT(updateFolderList()));
		updateFolderList();
	} else {
		kDebug(2300) << "Wallet open failed!" << endl;
	}

	createActions();
	createGUI("kwalleteditor.rc");
	delete toolBar();

	setCaption(wallet);

	QTimer::singleShot(0, this, SLOT(layout()));
}

KWalletEditor::~KWalletEditor() {
	emit editorClosed(this);
	delete _newFolderAction;
	_newFolderAction = 0L;
	delete _deleteFolderAction;
	_deleteFolderAction = 0L;
	delete _w;
	_w = 0L;
	if (_nonLocal) {
		KWallet::Wallet::closeWallet(_walletName, true);
	}
}

void KWalletEditor::layout() {
	QList<int> sz = _ww->_splitter->sizes();
	int sum = sz[0] + sz[1];
	sz[0] = sum/2;
	sz[1] = sum/2;
	_ww->_splitter->setSizes(sz);
}

void KWalletEditor::createActions() {
	_newFolderAction = new KAction(KIcon("folder_new"), i18n("&New Folder..."), actionCollection(), "create_folder");
	connect(_newFolderAction, SIGNAL(triggered(bool) ), SLOT(createFolder()));
	connect(this, SIGNAL(enableFolderActions(bool)),
		_newFolderAction, SLOT(setEnabled(bool)));

	_deleteFolderAction = new KAction(i18n("&Delete Folder"), actionCollection(), "delete_folder");
	connect(_deleteFolderAction, SIGNAL(triggered(bool) ), SLOT(deleteFolder()));
	connect(this, SIGNAL(enableContextFolderActions(bool)),
		_deleteFolderAction, SLOT(setEnabled(bool)));
	connect(this, SIGNAL(enableFolderActions(bool)),
		_deleteFolderAction, SLOT(setEnabled(bool)));

	_passwordAction = new KAction(i18n("Change &Password..."), actionCollection(), "change_password");
	connect(_passwordAction, SIGNAL(triggered(bool) ), SLOT(changePassword()));
	connect(this, SIGNAL(enableWalletActions(bool)),
		_passwordAction, SLOT(setEnabled(bool)));

	_mergeAction = new KAction(i18n("&Merge Wallet..."), actionCollection(), "merge");
	connect(_mergeAction, SIGNAL(triggered(bool) ), SLOT(importWallet()));
	connect(this, SIGNAL(enableWalletActions(bool)),
		_mergeAction, SLOT(setEnabled(bool)));

	_importAction = new KAction(i18n("&Import XML..."), actionCollection(), "import");
	connect(_importAction, SIGNAL(triggered(bool) ), SLOT(importXML()));
	connect(this, SIGNAL(enableWalletActions(bool)),
		_importAction, SLOT(setEnabled(bool)));

	_exportAction = new KAction(i18n("&Export..."), actionCollection(), "export");
	connect(_exportAction, SIGNAL(triggered(bool) ), SLOT(exportXML()));
	connect(this, SIGNAL(enableWalletActions(bool)),
		_exportAction, SLOT(setEnabled(bool)));

	_saveAsAction = KStdAction::saveAs(this, SLOT(saveAs()), actionCollection());
	connect(this, SIGNAL(enableWalletActions(bool)),
		_saveAsAction, SLOT(setEnabled(bool)));

	KStdAction::quit(this, SLOT(close()), actionCollection());
	KStdAction::keyBindings(guiFactory(), SLOT(configureShortcuts()),
actionCollection());
	emit enableWalletActions(false);
	emit enableFolderActions(false);
	emit enableContextFolderActions(false);
}


void KWalletEditor::walletClosed() {
	delete _w;
	_walletIsOpen = false;
	_w = 0L;
	_ww->setEnabled(false);
	emit enableWalletActions(false);
	emit enableFolderActions(false);
	KMessageBox::sorry(this, i18n("This wallet was forced closed.  You must reopen it to continue working with it."));
	deleteLater();
}


void KWalletEditor::updateFolderList(bool checkEntries) {
	QStringList fl = _w->folderList();
	Q3PtrStack<Q3ListViewItem> trash;

	for (Q3ListViewItem *i = _entryList->firstChild(); i; i = i->nextSibling()) {
		KWalletFolderItem *fi = dynamic_cast<KWalletFolderItem *>(i);
		if (!fi) {
			continue;
		}
		if (!fl.contains(fi->name())) {
			trash.push(i);
		}
	}

	trash.setAutoDelete(true);
	trash.clear();

	for (QStringList::Iterator i = fl.begin(); i != fl.end(); ++i) {
		if (_entryList->existsFolder(*i)) {
			if (checkEntries) {
				updateEntries(*i);
			}
			continue;
		}

		_w->setFolder(*i);
		QStringList entries = _w->entryList();
		KWalletFolderItem *item = new KWalletFolderItem(_w,_entryList,
			*i, entries.count());

		KWalletContainerItem *pi = new KWalletContainerItem(item, i18n("Passwords"),KWallet::Wallet::Password);
		KWalletContainerItem *mi = new KWalletContainerItem(item, i18n("Maps"),KWallet::Wallet::Map);
		KWalletContainerItem *bi = new KWalletContainerItem(item, i18n("Binary Data"),KWallet::Wallet::Stream);
		KWalletContainerItem *ui = new KWalletContainerItem(item, i18n("Unknown"),KWallet::Wallet::Unknown);

		for (QStringList::Iterator j = entries.begin(); j != entries.end(); ++j) {
			switch (_w->entryType(*j)) {
				case KWallet::Wallet::Password:
					new KWalletEntryItem(_w, pi, *j);
					break;
				case KWallet::Wallet::Stream:
					new KWalletEntryItem(_w, bi, *j);
					break;
				case KWallet::Wallet::Map:
					new KWalletEntryItem(_w, mi, *j);
					break;
				case KWallet::Wallet::Unknown:
				default:
					new Q3ListViewItem(ui, *j);
					break;
			}
		}
		_entryList->setEnabled(true);
	}

	//check if the current folder has been removed
	if (!fl.contains(_currentFolder)) {
		_currentFolder = "";
		_ww->_entryTitle->clear();
		_ww->_iconTitle->clear();
	}
}

void KWalletEditor::deleteFolder() {
	if (_w) {
		Q3ListViewItem *i = _entryList->currentItem();
		if (i) {
			KWalletFolderItem *fi = dynamic_cast<KWalletFolderItem *>(i);
			if (!fi) {
				return;
			}

			int rc = KMessageBox::warningContinueCancel(this, i18n("Are you sure you wish to delete the folder '%1' from the wallet?", fi->name()),"",KStdGuiItem::del());
			if (rc == KMessageBox::Continue) {
				bool rc = _w->removeFolder(fi->name());
				if (!rc) {
					KMessageBox::sorry(this, i18n("Error deleting folder."));
					return;
				}
				_currentFolder = "";
				_ww->_entryTitle->clear();
				_ww->_iconTitle->clear();
				updateFolderList();
			}
		}
	}
}


void KWalletEditor::createFolder() {
	if (_w) {
		QString n;
		bool ok;

		do {
			n = KInputDialog::getText(i18n("New Folder"),
					i18n("Please choose a name for the new folder:"),
					QString::null,
					&ok,
					this);

			if (!ok) {
				return;
			}

			if (_entryList->existsFolder(n)) {
				int rc = KMessageBox::questionYesNo(this, i18n("Sorry, that folder name is in use. Try again?"), QString::null, KGuiItem(i18n("Try Again")), KGuiItem(i18n("Do Not Try")));
				if (rc == KMessageBox::Yes) {
					continue;
				}
				n = QString::null;
			}
			break;
		} while (true);

		_w->createFolder(n);
		updateFolderList();
	}
}


void KWalletEditor::saveEntry() {
	int rc = 1;
	Q3ListViewItem *item = _entryList->currentItem();
	_ww->_saveChanges->setEnabled(false);
	_ww->_undoChanges->setEnabled(false);

	if (item && _w && item->parent()) {
		KWalletContainerItem *ci = dynamic_cast<KWalletContainerItem*>(item->parent());
		if (ci) {
			if (ci->type() == KWallet::Wallet::Password) {
				rc = _w->writePassword(item->text(0), _ww->_passwordValue->text());
			} else if (ci->type() == KWallet::Wallet::Map) {
				_mapEditor->saveMap();
				rc = _w->writeMap(item->text(0), _currentMap);
			} else {
				return;
			}

			if (rc == 0) {
				return;
			}
		}
	}

	KMessageBox::sorry(this, i18n("Error saving entry. Error code: %1", rc));
}


void KWalletEditor::restoreEntry() {
	entrySelectionChanged(_entryList->currentItem());
}


void KWalletEditor::entryEditted() {
	_ww->_saveChanges->setEnabled(true);
	_ww->_undoChanges->setEnabled(true);
}


void KWalletEditor::entrySelectionChanged(Q3ListViewItem *item) {
	KWalletContainerItem *ci = 0L;
	KWalletFolderItem *fi = 0L;

	switch (item->rtti()) {
		case KWalletEntryItemClass:
			ci = dynamic_cast<KWalletContainerItem*>(item->parent());
			if (!ci) {
				return;
			}
			fi = dynamic_cast<KWalletFolderItem*>(ci->parent());
			if (!fi) {
				return;
			}
			_w->setFolder(fi->name());
			_deleteFolderAction->setEnabled(false);
			if (ci->type() == KWallet::Wallet::Password) {
				QString pass;
				if (_w->readPassword(item->text(0), pass) == 0) {
					_ww->_entryStack->setCurrentIndex(4);
					_ww->_entryName->setText(i18n("Password: %1",
							 item->text(0)));
					_ww->_passwordValue->setText(pass);
					_ww->_saveChanges->setEnabled(false);
					_ww->_undoChanges->setEnabled(false);
				}
			} else if (ci->type() == KWallet::Wallet::Map) {
				_ww->_entryStack->setCurrentIndex(2);
				_mapEditorShowHide->setChecked(false);
				showHideMapEditorValue(false);
				if (_w->readMap(item->text(0), _currentMap) == 0) {
					_mapEditor->reload();
					_ww->_entryName->setText(i18n("Name-Value Map: %1", item->text(0)));
					_ww->_saveChanges->setEnabled(false);
					_ww->_undoChanges->setEnabled(false);
				}
			} else if (ci->type() == KWallet::Wallet::Stream) {
				_ww->_entryStack->setCurrentIndex(3);
				QByteArray ba;
				if (_w->readEntry(item->text(0), ba) == 0) {
					_ww->_entryName->setText(i18n("Binary Data: %1",
							 item->text(0)));
					_ww->_saveChanges->setEnabled(false);
					_ww->_undoChanges->setEnabled(false);
				}
			}
			break;

		case KWalletContainerItemClass:
			fi = dynamic_cast<KWalletFolderItem*>(item->parent());
			if (!fi) {
				return;
			}
			_w->setFolder(fi->name());
			_deleteFolderAction->setEnabled(false);
			_ww->_entryName->clear();
			_ww->_entryStack->setCurrentIndex(0);
			break;

		case KWalletFolderItemClass:
			fi = dynamic_cast<KWalletFolderItem*>(item);
			if (!fi) {
				return;
			}
			_w->setFolder(fi->name());
			_deleteFolderAction->setEnabled(true);
			_ww->_entryName->clear();
			_ww->_entryStack->setCurrentIndex(0);
			break;
	}

	if (fi) {
		_currentFolder = fi->name();
		_ww->_entryTitle->setText(QString("<font size=\"+1\">%1</font>").arg(fi->text(0)));
		_ww->_iconTitle->setPixmap(fi->getFolderIcon(K3Icon::Toolbar));
	}
}

void KWalletEditor::updateEntries(const QString& folder) {
	Q3PtrStack<Q3ListViewItem> trash;

	_w->setFolder(folder);
	QStringList entries = _w->entryList();

	KWalletFolderItem *fi = _entryList->getFolder(folder);

	if (!fi) {
		return;
	}

	KWalletContainerItem *pi = fi->getContainer(KWallet::Wallet::Password);
	KWalletContainerItem *mi = fi->getContainer(KWallet::Wallet::Map);
	KWalletContainerItem *bi = fi->getContainer(KWallet::Wallet::Stream);
	KWalletContainerItem *ui = fi->getContainer(KWallet::Wallet::Unknown);

	// Remove deleted entries
	for (Q3ListViewItem *i = pi->firstChild(); i; i = i->nextSibling()) {
		if (!entries.contains(i->text(0))) {
			if (i == _entryList->currentItem()) {
				entrySelectionChanged(0L);
			}
			trash.push(i);
		}
	}

	for (Q3ListViewItem *i = mi->firstChild(); i; i = i->nextSibling()) {
		if (!entries.contains(i->text(0))) {
			if (i == _entryList->currentItem()) {
				entrySelectionChanged(0L);
			}
			trash.push(i);
		}
	}

	for (Q3ListViewItem *i = bi->firstChild(); i; i = i->nextSibling()) {
		if (!entries.contains(i->text(0))) {
			if (i == _entryList->currentItem()) {
				entrySelectionChanged(0L);
			}
			trash.push(i);
		}
	}

	for (Q3ListViewItem *i = ui->firstChild(); i; i = i->nextSibling()) {
		if (!entries.contains(i->text(0))) {
			if (i == _entryList->currentItem()) {
				entrySelectionChanged(0L);
			}
			trash.push(i);
		}
	}

	trash.setAutoDelete(true);
	trash.clear();

	// Add new entries
	for (QStringList::Iterator i = entries.begin(); i != entries.end(); ++i) {
		if (fi->contains(*i)){
			continue;
		}

		switch (_w->entryType(*i)) {
			case KWallet::Wallet::Password:
				new KWalletEntryItem(_w, pi, *i);
				break;
			case KWallet::Wallet::Stream:
				new KWalletEntryItem(_w, bi, *i);
				break;
			case KWallet::Wallet::Map:
				new KWalletEntryItem(_w, mi, *i);
				break;
			case KWallet::Wallet::Unknown:
			default:
				new Q3ListViewItem(ui, *i);
				break;
		}
	}
	fi->refresh();
	if (fi->name() == _currentFolder) {
		_ww->_entryTitle->setText(QString("<font size=\"+1\">%1</font>").arg(fi->text(0)));
	}
	if (!_entryList->selectedItem()) {
		_ww->_entryName->clear();
		_ww->_entryStack->setCurrentIndex(0);
	}
}

void KWalletEditor::listContextMenuRequested(Q3ListViewItem *item, const QPoint& pos, int col) {
	Q_UNUSED(col)

	if (!_walletIsOpen) {
		return;
	}

	KWalletListItemClasses menuClass = KWalletUnknownClass;
	KWalletContainerItem *ci = 0L;

	if (item) {
		if (item->rtti() == KWalletEntryItemClass) {
			ci = dynamic_cast<KWalletContainerItem *>(item->parent());
			if (!ci) {
				return;
			}
		} else if (item->rtti() == KWalletContainerItemClass) {
			ci = dynamic_cast<KWalletContainerItem *>(item);
			if (!ci) {
				return;
			}
		}

		if (ci && ci->type() == KWallet::Wallet::Unknown) {
			return;
		}
		menuClass = static_cast<KWalletListItemClasses>(item->rtti());
	}

	KMenu *m = new KMenu(this);
	if (item) {
		QString title = item->text(0);
		// I think 200 pixels is wide enough for a title
		title = KStringHandler::cPixelSqueeze(title, m->fontMetrics(), 200);
		m->addTitle(title);
		switch (menuClass) {
			case KWalletEntryItemClass:
				m->insertItem(i18n("&New..." ), this, SLOT(newEntry()), Qt::Key_Insert);
				m->insertItem(i18n( "&Rename" ), this, SLOT(renameEntry()), Qt::Key_F2);
				m->insertItem(i18n( "&Delete" ), this, SLOT(deleteEntry()), Qt::Key_Delete);
				if (ci && ci->type() == KWallet::Wallet::Password) {
					m->insertSeparator();
					m->addAction( _copyPassAction );
				}
				break;

			case KWalletContainerItemClass:
				m->insertItem(i18n( "&New..." ), this, SLOT(newEntry()), Qt::Key_Insert);
				break;

			case KWalletFolderItemClass:
				m->addAction( _newFolderAction );
				m->addAction( _deleteFolderAction );
				break;
			default:
				abort();
				;
		}
	} else {
		m->addAction( _newFolderAction );
	}
	m->popup(pos);
}


void KWalletEditor::copyPassword() {
	Q3ListViewItem *item = _entryList->selectedItem();
	if (_w && item) {
		QString pass;
		if (_w->readPassword(item->text(0), pass) == 0) {
			QApplication::clipboard()->setText(pass);
		}
	}
}


void KWalletEditor::newEntry() {
	Q3ListViewItem *item = _entryList->selectedItem();
	QString n;
	bool ok;

	Q3ListViewItem *p;
	KWalletFolderItem *fi;

	//set the folder where we're trying to create the new entry
	if (_w && item) {
		p = item;
		if (p->rtti() == KWalletEntryItemClass) {
			p = item->parent();
		}
		fi = dynamic_cast<KWalletFolderItem *>(p->parent());
		if (!fi) {
			return;
		}
		_w->setFolder(fi->name());
	} else {
		return;
	}

	do {
		n = KInputDialog::getText(i18n("New Entry"),
				i18n("Please choose a name for the new entry:"),
				QString::null,
				&ok,
				this);

		if (!ok) {
			return;
		}

		// FIXME: prohibits the use of the subheadings
		if (fi->contains(n)) {
			int rc = KMessageBox::questionYesNo(this, i18n("Sorry, that entry already exists. Try again?"), QString::null, KGuiItem(i18n("Try Again")), KGuiItem(i18n("Do Not Try")));
			if (rc == KMessageBox::Yes) {
				continue;
			}
			n = QString::null;
		}
		break;
	} while (true);

	if (_w && item && !n.isEmpty()) {
		Q3ListViewItem *p = item;
		if (p->rtti() == KWalletEntryItemClass) {
			p = item->parent();
		}

		KWalletFolderItem *fi = dynamic_cast<KWalletFolderItem *>(p->parent());
		if (!fi) {
			KMessageBox::error(this, i18n("An unexpected error occurred trying to add the new entry"));
			return;
		}
		_w->setFolder(fi->name());

		KWalletEntryItem *ni = new KWalletEntryItem(_w, p, n);
		_entryList->setSelected(ni,true);
		_entryList->ensureItemVisible(ni);

		KWalletContainerItem *ci = dynamic_cast<KWalletContainerItem*>(p);
		if (!ci) {
			KMessageBox::error(this, i18n("An unexpected error occurred trying to add the new entry"));
			return;
		}
		if (ci->type() == KWallet::Wallet::Password) {
			_w->writePassword(n, QString::null);
		} else if (ci->type() == KWallet::Wallet::Map) {
			_w->writeMap(n, QMap<QString,QString>());
		} else if (ci->type() == KWallet::Wallet::Stream) {
			_w->writeEntry(n, QByteArray());
		} else {
			abort();
		}
		fi->refresh();
		_ww->_entryTitle->setText(QString("<font size=\"+1\">%1</font>").arg(fi->text(0)));
	}
}


void KWalletEditor::renameEntry() {
	Q3ListViewItem *item = _entryList->selectedItem();
	if (_w && item) {
		item->startRename(0);
	}
}


// Only supports renaming of KWalletEntryItem derived classes.
void KWalletEditor::listItemRenamed(Q3ListViewItem* item, int, const QString& t) {
	if (item) {
		KWalletEntryItem *i = dynamic_cast<KWalletEntryItem*>(item);
		if (!i) {
			return;
		}

		if (!_w || t.isEmpty()) {
			i->setText(0, i->oldName());
			return;
		}

		if (_w->renameEntry(i->oldName(), t) == 0) {
			i->clearOldName();
			KWalletContainerItem *ci = dynamic_cast<KWalletContainerItem*>(item->parent());
			if (!ci) {
				KMessageBox::error(this, i18n("An unexpected error occurred trying to rename the entry"));
				return;
			}
			if (ci->type() == KWallet::Wallet::Password) {
				_ww->_entryName->setText(i18n("Password: %1", item->text(0)));
			} else if (ci->type() == KWallet::Wallet::Map) {
				_ww->_entryName->setText(i18n("Name-Value Map: %1", item->text(0)));
			} else if (ci->type() == KWallet::Wallet::Stream) {
				_ww->_entryName->setText(i18n("Binary Data: %1", item->text(0)));
			}
		} else {
			i->setText(0, i->oldName());
		}
	}
}


void KWalletEditor::deleteEntry() {
	Q3ListViewItem *item = _entryList->selectedItem();
	if (_w && item) {
		int rc = KMessageBox::warningContinueCancel(this, i18n("Are you sure you wish to delete the item '%1'?", item->text(0)),"",KStdGuiItem::del());
		if (rc == KMessageBox::Continue) {
			KWalletFolderItem *fi = dynamic_cast<KWalletFolderItem *>(item->parent()->parent());
			if (!fi) {
				KMessageBox::error(this, i18n("An unexpected error occurred trying to delete the entry"));
				return;
			}
			_w->removeEntry(item->text(0));
			delete item;
			entrySelectionChanged(_entryList->currentItem());
			fi->refresh();
			_ww->_entryTitle->setText(QString("<font size=\"+1\">%1</font>").arg(fi->text(0)));
		}
	}
}

void KWalletEditor::changePassword() {
	KWallet::Wallet::changePassword(_walletName);
}


void KWalletEditor::walletOpened(bool success) {
	if (success) {
		emit enableFolderActions(true);
		emit enableContextFolderActions(false);
		emit enableWalletActions(true);
		updateFolderList();
		show();
		_entryList->setWallet(_w);
		_walletIsOpen = true;
	} else {
		if (!_newWallet) {
			KMessageBox::sorry(this, i18n("Unable to open the requested wallet."));
		}
		close();
	}
}


void KWalletEditor::hidePasswordContents() {
	_ww->_entryStack->setCurrentIndex(4);
}


void KWalletEditor::showPasswordContents() {
	_ww->_entryStack->setCurrentIndex(1);
}


void KWalletEditor::showHideMapEditorValue(bool show) {
	if (show) {
		_mapEditor->showColumn(2);
	} else {
		_mapEditor->hideColumn(2);
	}
}


enum MergePlan { Prompt = 0, Always = 1, Never = 2, Yes = 3, No = 4 };

void KWalletEditor::importWallet() {
	KUrl url = KFileDialog::getOpenUrl(KUrl(), "*.kwl", this);
	if (url.isEmpty()) {
		return;
	}

	QString tmpFile;
	if (!KIO::NetAccess::download(url, tmpFile, this)) {
		KMessageBox::sorry(this, i18n("Unable to access wallet '<b>%1</b>'.", url.prettyUrl()));
		return;
	}

	KWallet::Wallet *w = KWallet::Wallet::openWallet(tmpFile, winId(), KWallet::Wallet::Path);
	if (w && w->isOpen()) {
		MergePlan mp = Prompt;
		QStringList fl = w->folderList();
		for (QStringList::ConstIterator f = fl.constBegin(); f != fl.constEnd(); ++f) {
			if (!w->setFolder(*f)) {
				continue;
			}

			if (!_w->hasFolder(*f)) {
				_w->createFolder(*f);
			}

			_w->setFolder(*f);

			QMap<QString, QMap<QString, QString> > map;
			int rc;
			rc = w->readMapList("*", map);
			if (rc == 0) {
				QMap<QString, QMap<QString, QString> >::ConstIterator me;
				for (me = map.constBegin(); me != map.constEnd(); ++me) {
					bool hasEntry = _w->hasEntry(me.key());
					if (hasEntry && mp == Prompt) {
						KBetterThanKDialogBase *bd;
						bd = new KBetterThanKDialogBase(this);
						bd->setLabel(i18n("Folder '<b>%1</b>' already contains an entry '<b>%2</b>'.  Do you wish to replace it?", Qt::escape(*f), Qt::escape(me.key())));
						mp = (MergePlan)bd->exec();
						delete bd;
						bool ok = false;
						if (mp == Always || mp == Yes) {
							ok = true;
						}
						if (mp == Yes || mp == No) {
						       	// reset mp
							mp = Prompt;
						}
						if (!ok) {
							continue;
						}
					} else if (hasEntry && mp == Never) {
						continue;
					}
					_w->writeMap(me.key(), me.value());
				}
			}

			QMap<QString, QString> pwd;
			rc = w->readPasswordList("*", pwd);
			if (rc == 0) {
				QMap<QString, QString>::ConstIterator pe;
				for (pe = pwd.constBegin(); pe != pwd.constEnd(); ++pe) {
					bool hasEntry = _w->hasEntry(pe.key());
					if (hasEntry && mp == Prompt) {
						KBetterThanKDialogBase *bd;
						bd = new KBetterThanKDialogBase(this);
						bd->setLabel(i18n("Folder '<b>%1</b>' already contains an entry '<b>%2</b>'.  Do you wish to replace it?", Qt::escape(*f), Qt::escape(pe.key())));
						mp = (MergePlan)bd->exec();
						delete bd;
						bool ok = false;
						if (mp == Always || mp == Yes) {
							ok = true;
						}
						if (mp == Yes || mp == No) {
						       	// reset mp
							mp = Prompt;
						}
						if (!ok) {
							continue;
						}
					} else if (hasEntry && mp == Never) {
						continue;
					}
					_w->writePassword(pe.key(), pe.value());
				}
			}

			QMap<QString, QByteArray> ent;
			rc = w->readEntryList("*", ent);
			if (rc == 0) {
				QMap<QString, QByteArray>::ConstIterator ee;
				for (ee = ent.constBegin(); ee != ent.constEnd(); ++ee) {
					bool hasEntry = _w->hasEntry(ee.key());
					if (hasEntry && mp == Prompt) {
						KBetterThanKDialogBase *bd;
						bd = new KBetterThanKDialogBase(this);
						bd->setLabel(i18n("Folder '<b>%1</b>' already contains an entry '<b>%2</b>'.  Do you wish to replace it?", Qt::escape(*f), Qt::escape(ee.key())));
						mp = (MergePlan)bd->exec();
						delete bd;
						bool ok = false;
						if (mp == Always || mp == Yes) {
							ok = true;
						}
						if (mp == Yes || mp == No) {
						       	// reset mp
							mp = Prompt;
						}
						if (!ok) {
							continue;
						}
					} else if (hasEntry && mp == Never) {
						continue;
					}
					_w->writeEntry(ee.key(), ee.value());
				}
			}
		}
	}

	delete w;

	KIO::NetAccess::removeTempFile(tmpFile);
	updateFolderList(true);
	restoreEntry();
}


void KWalletEditor::importXML() {
	KUrl url = KFileDialog::getOpenUrl( KUrl(), "*.xml", this);
	if (url.isEmpty()) {
		return;
	}

	QString tmpFile;
	if (!KIO::NetAccess::download(url, tmpFile, this)) {
		KMessageBox::sorry(this, i18n("Unable to access XML file '<b>%1</b>'.", url.prettyUrl()));
		return;
	}

	QFile qf(tmpFile);
	if (!qf.open(QIODevice::ReadOnly)) {
		KMessageBox::sorry(this, i18n("Error opening XML file '<b>%1</b>' for input.", url.prettyUrl()));
		KIO::NetAccess::removeTempFile(tmpFile);
		return;
	}

	QDomDocument doc(tmpFile);
	if (!doc.setContent(&qf)) {
		KMessageBox::sorry(this, i18n("Error reading XML file '<b>%1</b>' for input.", url.prettyUrl()));
		KIO::NetAccess::removeTempFile(tmpFile);
		return;
	}

	QDomElement top = doc.documentElement();
	if (top.tagName().toLower() != "wallet") {
		KMessageBox::sorry(this, i18n("Error: XML file does not contain a wallet."));
		KIO::NetAccess::removeTempFile(tmpFile);
		return;
	}

	QDomNode n = top.firstChild();
	MergePlan mp = Prompt;
	while (!n.isNull()) {
		QDomElement e = n.toElement();
		if (e.tagName().toLower() != "folder") {
			n = n.nextSibling();
			continue;
		}

		QString fname = e.attribute("name");
		if (fname.isEmpty()) {
			n = n.nextSibling();
			continue;
		}
		if (!_w->hasFolder(fname)) {
			_w->createFolder(fname);
		}
		_w->setFolder(fname);
		QDomNode enode = e.firstChild();
		while (!enode.isNull()) {
			e = enode.toElement();
			QString type = e.tagName().toLower();
			QString ename = e.attribute("name");
			bool hasEntry = _w->hasEntry(ename);
			if (hasEntry && mp == Prompt) {
				KBetterThanKDialogBase *bd;
				bd = new KBetterThanKDialogBase(this);
				bd->setLabel(i18n("Folder '<b>%1</b>' already contains an entry '<b>%2</b>'.  Do you wish to replace it?", Qt::escape(fname), Qt::escape(ename)));
				mp = (MergePlan)bd->exec();
				delete bd;
				bool ok = false;
				if (mp == Always || mp == Yes) {
					ok = true;
				}
				if (mp == Yes || mp == No) { // reset mp
					mp = Prompt;
				}
				if (!ok) {
					enode = enode.nextSibling();
					continue;
				}
			} else if (hasEntry && mp == Never) {
				enode = enode.nextSibling();
				continue;
			}

			if (type == "password") {
				_w->writePassword(ename, e.text());
			} else if (type == "stream") {
				_w->writeEntry(ename, KCodecs::base64Decode(e.text().toLatin1()));
			} else if (type == "map") {
				QMap<QString,QString> map;
				QDomNode mapNode = e.firstChild();
				while (!mapNode.isNull()) {
					QDomElement mape = mapNode.toElement();
					if (mape.tagName().toLower() == "mapentry") {
						map[mape.attribute("name")] = mape.text();
					}
					mapNode = mapNode.nextSibling();
				}
				_w->writeMap(ename, map);
			}
			enode = enode.nextSibling();
		}
		n = n.nextSibling();
	}

	KIO::NetAccess::removeTempFile(tmpFile);
	updateFolderList(true);
	restoreEntry();
}


void KWalletEditor::exportXML() {
	KTempFile tf;
	tf.setAutoDelete(true);
	QTextStream& ts(*tf.textStream());
	QStringList fl = _w->folderList();

	ts << "<wallet name=\"" << _walletName << "\">" << endl;
	for (QStringList::Iterator i = fl.begin(); i != fl.end(); ++i) {
		ts << "  <folder name=\"" << *i << "\">" << endl;
		_w->setFolder(*i);
		QStringList entries = _w->entryList();
		for (QStringList::Iterator j = entries.begin(); j != entries.end(); ++j) {
			switch (_w->entryType(*j)) {
				case KWallet::Wallet::Password:
					{
						QString pass;
						if (_w->readPassword(*j, pass) == 0) {
							ts << "    <password name=\"" << Qt::escape(*j) << "\">";
							ts << Qt::escape(pass);
							ts << "</password>" << endl;
						}
						break;
					}
				case KWallet::Wallet::Stream:
					{
						QByteArray ba;
						if (_w->readEntry(*j, ba) == 0) {
							ts << "    <stream name=\"" << Qt::escape(*j) << "\">";
							ts << KCodecs::base64Encode(ba);

							ts << "</stream>" << endl;
						}
						break;
					}
				case KWallet::Wallet::Map:
					{
						QMap<QString,QString> map;
						if (_w->readMap(*j, map) == 0) {
							ts << "    <map name=\"" << Qt::escape(*j) << "\">" << endl;
							for (QMap<QString,QString>::ConstIterator k = map.begin(); k != map.end(); ++k) {
								ts << "      <mapentry name=\"" << Qt::escape(k.key()) << "\">" << Qt::escape(k.value()) << "</mapentry>" << endl;
							}
							ts << "    </map>" << endl;
						}
						break;
					}
				case KWallet::Wallet::Unknown:
				default:
					break;
			}
		}
		ts << "  </folder>" << endl;
	}

	ts << "</wallet>" << endl;
	tf.close();

	KUrl url = KFileDialog::getSaveUrl(KUrl(), "*.xml", this);

	if (!url.isEmpty()) {
		KIO::NetAccess::dircopy(KUrl::fromPath(tf.name()), url, this);
	}
}


void KWalletEditor::setNewWallet(bool x) {
	_newWallet = x;
}


void KWalletEditor::saveAs() {
	KUrl url = KFileDialog::getSaveUrl(KUrl(), "*.kwl", this);
	if (!url.isEmpty()) {
		// Sync() kwalletd
		if (_nonLocal) {
			KIO::NetAccess::dircopy(_walletName, url, this);
		} else {
			QString path = KGlobal::dirs()->saveLocation("kwallet") + "/" + _walletName + ".kwl";
			KIO::NetAccess::dircopy(KUrl::fromPath(path), url, this);
		}
	}
}


#include "kwalleteditor.moc"

