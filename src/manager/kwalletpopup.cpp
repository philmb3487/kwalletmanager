/*
   Copyright (C) 2003 George Staikos <staikos@kde.org>

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

#include "kwalletpopup.h"

#include <kstandardaction.h>
#include <kactioncollection.h>
#include <QAction>
#include <klocalizedstring.h>
#include <kmessagebox.h>
#include <kwallet.h>


KWalletPopup::KWalletPopup(const QString &wallet, QWidget *parent, const QString &name)
    : QMenu(parent), _walletName(wallet)
{
    addSection(wallet);
    setObjectName(name);
    KActionCollection *ac = new KActionCollection(this/*, "kwallet context actions"*/);
    ac->setObjectName(QStringLiteral("kwallet context actions"));
    QAction *act;

    act = ac->addAction(QStringLiteral("wallet_create"));
    act->setText(i18n("&New Wallet..."));
    connect(act, &QAction::triggered, this, &KWalletPopup::createWallet);
    addAction(act);

    act = ac->addAction(QStringLiteral("wallet-open"));
    act->setText(i18n("&Open..."));
    connect(act, &QAction::triggered, this, &KWalletPopup::openWallet);
    act->setShortcut(QKeySequence(Qt::Key_Return));
    addAction(act);

    act = ac->addAction(QStringLiteral("wallet_password"));
    act->setText(i18n("Change &Password..."));
    connect(act, &QAction::triggered, this, &KWalletPopup::changeWalletPassword);
    addAction(act);

    const QStringList ul = KWallet::Wallet::users(wallet);
    if (!ul.isEmpty()) {
        QMenu *pm = new QMenu(this);
        pm->setObjectName(QStringLiteral("Disconnect Apps"));
        int id = 7000;
        for (QStringList::const_iterator it = ul.begin(), end(ul.end()); it != end; ++it) {
            QAction *a = pm->addAction(*it, this, &KWalletPopup::disconnectApp);
            a->setData(*it);
            id++;
        }
        QAction *act = addMenu(pm);
        act->setText(i18n("Disconnec&t"));
    }

    act = KStandardAction::close(this,
                                 SLOT(closeWallet()), ac);
    ac->addAction(QStringLiteral("wallet_close"), act);
    // FIXME: let's track this inside the manager so we don't need a dcop
    //        roundtrip here.
    act->setEnabled(KWallet::Wallet::isOpen(wallet));
    addAction(act);

    act = ac->addAction(QStringLiteral("wallet_delete"));
    act->setText(i18n("&Delete"));

    connect(act, &QAction::triggered, this, &KWalletPopup::deleteWallet);
    act->setShortcut(QKeySequence(Qt::Key_Delete));
    addAction(act);
}

KWalletPopup::~KWalletPopup()
{
}

void KWalletPopup::openWallet()
{
    emit walletOpened(_walletName);
}

void KWalletPopup::deleteWallet()
{
    emit walletDeleted(_walletName);
}

void KWalletPopup::closeWallet()
{
    emit walletClosed(_walletName);
}

void KWalletPopup::changeWalletPassword()
{
    emit walletChangePassword(_walletName);
}

void KWalletPopup::createWallet()
{
    emit walletCreated();
}

void KWalletPopup::disconnectApp()
{
    QAction *a = qobject_cast<QAction *>(sender());
    Q_ASSERT(a);
    if (a)     {
        KWallet::Wallet::disconnectApplication(_walletName, a->data().toString());
    }
}



