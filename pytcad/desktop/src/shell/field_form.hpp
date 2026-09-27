#pragma once

// A dynamically-built QLineEdit form field (name/label/default/tip) and
// the "read it as a finite double, name the first bad one" pattern used
// by every panel that hands a bag of numeric parameters to a solver job
// (RunPanel's Sweep/Transient/AC/Family/C-V forms, CompactModelPanel's
// Diode/MOSFET forms) -- hoisted here (code-review, 2026-09-27) after
// CompactModelPanel reimplemented both nearly verbatim.

#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QString>
#include <QWidget>

#include <cmath>
#include <optional>

namespace tcad::desktop {

struct FieldDef {
    const char* name;
    const char* label;
    const char* value;
    const char* tip = nullptr;
};

inline QLineEdit* addField(QFormLayout* form, const FieldDef& f, QWidget* parent) {
    auto* e = new QLineEdit(QString::fromLatin1(f.value), parent);
    e->setObjectName(QString::fromLatin1(f.name));
    if (f.tip && *f.tip) e->setToolTip(QString::fromLatin1(f.tip));
    e->setAccessibleName(QString::fromLatin1(f.label));
    form->addRow(QString::fromLatin1(f.label), e);
    return e;
}

// Reads `name`'s QLineEdit (a child of `parent`) as a finite double. On
// a parse failure or a non-finite value, records that field's
// accessible name into `bad` -- but only the FIRST time, across however
// many fields the caller reads this way -- so a caller that reads every
// field before checking `bad` reports the first offending one, not the
// last.
inline double numberField(QWidget* parent, const char* name, std::optional<QString>& bad) {
    QLineEdit* e = parent->findChild<QLineEdit*>(QString::fromLatin1(name));
    bool ok = false;
    const double v = QLocale::c().toDouble(e->text().trimmed(), &ok);
    if ((!ok || !std::isfinite(v)) && !bad) bad = e->accessibleName();
    return v;
}

}  // namespace tcad::desktop
