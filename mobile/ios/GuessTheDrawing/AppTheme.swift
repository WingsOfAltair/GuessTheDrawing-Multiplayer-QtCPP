import SwiftUI
import Combine

@MainActor
final class AppTheme: ObservableObject {
    @Published var selection: String { didSet { UserDefaults.standard.set(selection, forKey: "theme") } }
    init() { selection = UserDefaults.standard.string(forKey: "theme") ?? "system" }
    var palette: ThemePalette? { ThemePalette.all.first { $0.id == selection } }
    var scheme: ColorScheme? { palette.map { $0.dark ? .dark : .light } }
    var window: Color { palette.map { Color(uiColor: UIColor(hex: $0.window)) } ?? Color(uiColor: .systemGroupedBackground) }
    var surface: Color { palette.map { Color(uiColor: UIColor(hex: $0.surface)) } ?? Color(uiColor: .secondarySystemGroupedBackground) }
    var base: Color { palette.map { Color(uiColor: UIColor(hex: $0.base)) } ?? Color(uiColor: .systemBackground) }
    var text: Color { palette.map { Color(uiColor: UIColor(hex: $0.text)) } ?? .primary }
    var secondary: Color { palette.map { Color(uiColor: UIColor(hex: $0.disabledText)) } ?? .secondary }
    var accent: Color { palette.map { Color(uiColor: UIColor(hex: $0.accent)) } ?? .accentColor }
    var accentText: Color { palette.map { Color(uiColor: UIColor(hex: $0.accentText)) } ?? .white }
}
@MainActor
struct ThemeMenu: View {
    @EnvironmentObject private var theme: AppTheme
    var body: some View {
        Menu {
            Picker("Appearance", selection: $theme.selection) {
                Text("System").tag("system")
                ForEach(ThemePalette.all, id: \.id) { Text($0.name).tag($0.id) }
            }
        } label: { Label("Theme", systemImage: "paintpalette") }
    }
}
@MainActor
private struct ThemeSurface: ViewModifier {
    @EnvironmentObject var theme: AppTheme
    func body(content: Content) -> some View {
        content.scrollContentBackground(.hidden).background(theme.window)
            .foregroundStyle(theme.text).tint(theme.accent)
            .toolbarBackground(theme.surface, for: .navigationBar, .tabBar)
            .toolbarBackground(.visible, for: .navigationBar, .tabBar)
    }
}
@MainActor
private struct ThemeRow: ViewModifier {
    @EnvironmentObject var theme: AppTheme
    func body(content: Content) -> some View { content.listRowBackground(theme.surface).foregroundStyle(theme.text) }
}
extension View {
    @MainActor func gtdSurface() -> some View { modifier(ThemeSurface()) }
    @MainActor func gtdRow() -> some View { modifier(ThemeRow()) }
}
