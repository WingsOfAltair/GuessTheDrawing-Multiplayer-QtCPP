import SwiftUI
import Combine

@MainActor
final class AppLanguage: ObservableObject {
    @Published var selection = L10n.language {
        didSet { L10n.language = selection; UserDefaults.standard.set(selection, forKey: "language") }
    }
    var locale: Locale { L10n.locale }
    var direction: LayoutDirection { L10n.rtl ? .rightToLeft : .leftToRight }
}
@MainActor
struct LanguageMenu: View {
    @EnvironmentObject private var language: AppLanguage
    var body: some View {
        Menu {
            Picker(L("Language"), selection: $language.selection) {
                ForEach(L10n.languages, id: \.0) { code, name in Text(verbatim: name).tag(code) }
            }
        } label: { Label(L("Language"), systemImage: "globe") }
    }
}
