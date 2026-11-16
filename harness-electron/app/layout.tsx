import type { Metadata } from "next";
import "./globals.css";
export const metadata: Metadata = {
  title: "NextAgent Harness",
  description: "Native agent workspace",
};
export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="ru" className="dark" suppressHydrationWarning>
      <body>{children}</body>
    </html>
  );
}
