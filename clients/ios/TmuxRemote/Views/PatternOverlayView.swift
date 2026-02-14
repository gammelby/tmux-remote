import SwiftUI

struct PatternOverlayView: View {
    let match: PatternMatch
    let onAction: (ResolvedAction) -> Void
    let onDismiss: () -> Void
    @Environment(\.colorScheme) private var colorScheme

    var body: some View {
        VStack(spacing: 0) {
            // Drag handle
            RoundedRectangle(cornerRadius: 2.5)
                .fill(Color.secondary.opacity(0.4))
                .frame(width: 36, height: 5)
                .padding(.top, 8)
                .padding(.bottom, 4)

            // Prompt header
            if let prompt = match.prompt {
                Text(prompt)
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 20)
                    .padding(.top, 16)
                    .padding(.bottom, 12)
                    .accessibilityIdentifier("pattern-overlay-prompt")

                Divider()
            }

            // Action buttons
            actionButtons

            Divider()

            // Dismiss button
            Button {
                onDismiss()
            } label: {
                Text("Dismiss")
                    .font(.body)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
            }
            .accessibilityIdentifier("pattern-overlay-dismiss")
        }
        .background(
            RoundedRectangle(cornerRadius: 14)
                .fill(colorScheme == .dark ? Color.tmuxSurface : Color(.systemBackground))
        )
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .shadow(color: .black.opacity(0.2), radius: 10, y: -2)
        .padding(.horizontal, 40)
        .padding(.bottom, 8)
        .accessibilityIdentifier("pattern-overlay")
    }

    @ViewBuilder
    private var actionButtons: some View {
        switch match.patternType {
        case .yesNo, .acceptReject:
            binaryActions
        case .numberedMenu:
            menuActions
        }
    }

    @ViewBuilder
    private var binaryActions: some View {
        ForEach(Array(match.actions.enumerated()), id: \.element.id) { index, action in
            if index > 0 {
                Divider()
            }
            Button {
                onAction(action)
            } label: {
                Text(action.label)
                    .font(.body)
                    .fontWeight(index == 0 ? .semibold : .regular)
                    .foregroundColor(actionColor(for: action.label, isFirst: index == 0))
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
            }
            .accessibilityIdentifier("pattern-action-\(index)")
        }
    }

    @ViewBuilder
    private var menuActions: some View {
        let maxVisible = 5
        let items = match.actions

        if items.count > maxVisible {
            ScrollView {
                menuItemList(items: items)
            }
            .frame(maxHeight: CGFloat(maxVisible) * 48)
        } else {
            menuItemList(items: items)
        }
    }

    @ViewBuilder
    private func menuItemList(items: [ResolvedAction]) -> some View {
        ForEach(Array(items.enumerated()), id: \.element.id) { index, action in
            if index > 0 {
                Divider()
            }
            Button {
                onAction(action)
            } label: {
                Text(action.label)
                    .font(.body)
                    .foregroundColor(menuItemColor(for: action.label))
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 20)
                    .padding(.vertical, 12)
            }
            .accessibilityIdentifier("pattern-menu-item-\(index)")
        }
    }

    private func actionColor(for label: String, isFirst: Bool) -> Color {
        let lower = label.lowercased()
        if lower == "no" || lower == "deny" || lower == "reject" {
            return .tmuxDestructive
        }
        return .accentColor
    }

    private func menuItemColor(for label: String) -> Color {
        let lower = label.lowercased()
        if lower == "no" || lower.hasPrefix("no,") {
            return .tmuxDestructive
        }
        return .accentColor
    }
}
