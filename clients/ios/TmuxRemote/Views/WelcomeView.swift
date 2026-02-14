import SwiftUI

struct WelcomeView: View {
    @Binding var showPairing: Bool

    var body: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "terminal.fill")
                .font(.system(size: 64))
                .foregroundColor(.accentColor)

            Text("tmux-remote")
                .font(.largeTitle)
                .fontWeight(.bold)

            Text("Secure remote terminal, no SSH config")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 32)

            Spacer()

            Button {
                showPairing = true
            } label: {
                Label("Add Agent", systemImage: "plus")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding()
                    .background(Color.accentColor)
                    .foregroundColor(.white)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
            }
            .padding(.horizontal, 32)
            .padding(.bottom, 48)
        }
    }
}
